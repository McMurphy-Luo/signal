/**
 * @file state.h
 * @brief Lightweight observable state (state) and derived state (computed).
 *
 * Built on top of signals2 (signals.h). Header-only, no runtime registry, no
 * singleton, no virtual dispatch on the hot path. A state is a self-contained
 * object: declare one anywhere, exactly like a signal.
 *
 * Public surface:
 *   observable<T>  read-only interface; use as a parameter type
 *   state<T, Equal>    writable source of truth
 *   computed<T, Equal> read-only derived value, dependencies discovered automatically
 *   always_notify       equality policy that notifies on every set/recompute
 *
 * Example:
 *   struct Model {
 *     state<float>       raw_quality;   // source
 *     computed<CString>  quality_text;  // derived
 *   };
 *
 *   // in DoInit()
 *   model_.quality_text.bind([this] { return FormatQuality(model_.raw_quality.get()); });
 *   label_->SetText(model_.quality_text.get());
 *   conns_.push_back(model_.quality_text.connect(label_, &CLabelUI::SetText));
 *
 *   // anywhere
 *   model_.raw_quality.set(4.5f);       // quality_text recomputes, label_ refreshes
 *
 * Lifetime: a connect() call returns a connection; whoever owns the callback
 * target must own that connection. Store them in a plain
 * std::vector<signals2::connection> member -- connection is move-only, so the
 * vector is non-copyable, and everything disconnects on destruction. Never
 * store a connection in the object being observed -- that ties the
 * subscription to the wrong lifetime.
 *
 * Threading: not thread-safe, like signals2 itself. Use from a single thread
 * (in practice, the UI thread).
 *
 * Notification: observable is current state, not an event stream. Reentrant
 * changes are serialized per observable, obsolete intermediate notifications
 * are skipped, and the latest stable value is delivered before the outermost
 * update returns. Use signal2 directly when every transient event matters.
 *
 * Cycles: a computed whose own output feeds back into one of its dependencies
 * is a usage error. Feedback that settles is supported. An unbounded cycle may
 * exhaust the stack or loop; this class deliberately does not impose a
 * recovery policy.
 *
 * Known limitations:
 *
 * - mutate provides only the basic exception guarantee. If the mutator changes
 *   the value and then throws, no notification is emitted. Mutators should not
 *   throw after modifying the value.
 *
 * - No two-way binding. There is deliberately no "set without notifying", so
 *   writing back from an edit control inside its own change handler echoes to
 *   the view and resets the caret. Guard it at the call site with a flag; a
 *   silent set() would desynchronize every computed that depends on the value,
 *   which is worse.
 *
 * - Dependency tracking does not cross a DLL boundary when the get() call
 *   happens inside an exported non-inline function of another module: the
 *   tracking stack is thread_local per module. Reading another module's state
 *   directly from a compute function is fine -- that get() is instantiated in
 *   the calling module. Keep a computed and its compute function in one module.
 */

#ifndef SIGNALS2_STATE_H_
#define SIGNALS2_STATE_H_

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

#include "signals.h"

namespace signals2 {

/// Equality policy for values that cannot or should not be compared. Returning
/// false means every set/recompute is treated as a change and notifies.
struct always_notify {
  template <typename T>
  constexpr bool operator()(const T&, const T&) const noexcept {
    return false;
  }
};

namespace detail {

/**
 * @brief Sink for dependencies discovered while a compute function runs.
 *
 * Implemented by computed. Never part of the public contract: the tracking
 * mechanism must stay replaceable.
 */
class dependency_tracker {
public:
  virtual void on_dependency_changed() = 0;
  virtual void add_dependency(signals2::connection&& connection) = 0;
  /// @return true if @p dependency was not tracked yet (caller should subscribe)
  virtual bool try_mark_tracked(const void* dependency) = 0;

protected:
  ~dependency_tracker() = default;
};

/// A stack, not a single slot, so nested computed evaluation nests correctly.
inline std::vector<dependency_tracker*>& tracking_stack() {
  static thread_local std::vector<dependency_tracker*> stack;
  return stack;
}

inline dependency_tracker* current_tracker() {
  std::vector<dependency_tracker*>& stack = tracking_stack();
  return stack.empty() ? nullptr : stack.back();
}

class tracking_scope {
public:
  explicit tracking_scope(dependency_tracker* tracker) { tracking_stack().push_back(tracker); }
  ~tracking_scope() { tracking_stack().pop_back(); }

  tracking_scope(const tracking_scope&) = delete;
  tracking_scope& operator=(const tracking_scope&) = delete;
};

}  // namespace detail

/**
 * @brief Read-only view of an observable value.
 *
 * Use `const observable<T>&` as a parameter type to accept either a state or a
 * computed without caring which. Non-copyable and non-movable: trackers and
 * slots refer to the object by address.
 */
template <typename T>
class observable {
public:
  using value_type = T;
  using callback_type = std::function<void(const T&)>;

  observable(const observable&) = delete;
  observable& operator=(const observable&) = delete;
  observable(observable&&) = delete;
  observable& operator=(observable&&) = delete;

  /**
   * @brief Read the current value, registering a dependency if a computed is
   *        being evaluated right now.
   */
  const T& get() const {
    if (detail::dependency_tracker* tracker = detail::current_tracker()) {
      if (tracker->try_mark_tracked(this)) {
        tracker->add_dependency(sig_.connect(
            callback_type([tracker](const T&) { tracker->on_dependency_changed(); })));
      }
    }
    return value_;
  }

  /// Read without registering a dependency. Use inside a compute function for
  /// values that should not trigger a recompute.
  const T& peek() const { return value_; }

  /**
   * @brief Connect a callable taking either (const T&) or no arguments.
   *
   * @return connection; the caller must keep it alive for the subscription to
   *         stay alive. Marked [[nodiscard]] -- dropping it is always a bug.
   *
   * const because `const observable<T>&` is the read-only handle: a parameter
   * of that type must be able to observe, not just read once.
   */
  template <typename F>
    requires std::invocable<F, const T&> || std::invocable<F>
  [[nodiscard]] signals2::connection connect(F&& fn) const {
    // Only meaningful for std::function / function pointers; skipped for lambdas.
    if constexpr (requires { static_cast<bool>(fn); }) {
      if (!fn) {
        return signals2::connection();
      }
    }

    callback_type callback;
    if constexpr (std::invocable<F, const T&>) {
      callback = [f = std::forward<F>(fn)](const T& value) mutable { f(value); };
    } else {
      callback = [f = std::forward<F>(fn)](const T&) mutable { f(); };
    }

    return sig_.connect(std::move(callback));
  }

  /**
   * @brief Connect a member function.
   *
   * One overload covers every parameter type the value converts to:
   * void(const T&), void(T), and e.g. void(LPCTSTR) when T is CString and the
   * conversion is implicit. Anything else: pass a lambda.
   */
  template <typename C, typename M>
    requires std::is_member_function_pointer_v<M>
  [[nodiscard]] signals2::connection connect(C* instance, M method) const {
    if (!instance || !method) {
      return signals2::connection();
    }
    return connect([instance, method](const T& value) { (instance->*method)(value); });
  }

  /// notify subscribers with the current value without changing it. Rarely
  /// needed; initial synchronization is explicit at the call site.
  void notify() { emit(); }

protected:
  observable() = default;
  explicit observable(T init) : value_(std::move(init)) {}

  template <typename... Args>
    requires std::constructible_from<T, Args...>
  explicit observable(std::in_place_t, Args&&... args)
      : value_(std::forward<Args>(args)...) {}

  ~observable() = default;

  void emit() {
    ++revision_;

    if (emitting_) {
      pending_emit_ = true;
      return;
    }

    struct notification_guard {
      bool& emitting;
      bool& pending;

      ~notification_guard() {
        pending = false;
        emitting = false;
      }
    };

    emitting_ = true;
    notification_guard guard{emitting_, pending_emit_};
    do {
      pending_emit_ = false;
      const std::uint64_t emitted_revision = revision_;

      auto it = sig_.cbegin();
      // Connections added by a callback start receiving on the next emission.
      const auto end_it = sig_.cend();
      while (emitted_revision == revision_ && it != end_it) {
        if (*it) {
          (*it)(value_);
        }
        ++it;
      }
    } while (pending_emit_);
  }

  T value_{};
  mutable signals2::signal2<void(const T&)> sig_;
  std::uint64_t revision_ = 0;
  bool emitting_ = false;
  bool pending_emit_ = false;
};

/**
 * @brief Writable observable value.
 *
 * set() notifies only when Equal considers the new value different. Equal is
 * stored in the state and defaults to std::equal_to<>. A T without operator==
 * must provide an explicit equality predicate, or use always_notify. The set()
 * overloads participate only when T supports the corresponding assignment;
 * non-assignable values can still be changed in place with mutate().
 */
template <typename T, typename Equal = std::equal_to<>>
  requires std::predicate<Equal&, const T&, const T&>
class state final : public observable<T> {
public:
  state()
    requires std::default_initializable<T> &&
             std::default_initializable<Equal>
  = default;

  explicit state(T init)
    requires std::default_initializable<Equal>
      : observable<T>(std::move(init)) {}

  state(T init, Equal equal)
      : observable<T>(std::move(init)), equal_(std::move(equal)) {}

  template <typename... Args>
    requires std::default_initializable<Equal> &&
             std::constructible_from<T, Args...>
  explicit state(std::in_place_t, Args&&... args)
      : observable<T>(std::in_place, std::forward<Args>(args)...) {}

  template <typename... Args>
    requires std::constructible_from<T, Args...>
  state(Equal equal, std::in_place_t, Args&&... args)
      : observable<T>(std::in_place, std::forward<Args>(args)...),
        equal_(std::move(equal)) {}

  void set(const T& value)
    requires std::assignable_from<T&, const T&>
  {
    if (std::invoke(equal_, this->value_, value)) {
      return;
    }
    this->value_ = value;
    this->emit();
  }

  void set(T&& value)
    requires std::assignable_from<T&, T>
  {
    if (std::invoke(equal_, this->value_, value)) {
      return;
    }
    this->value_ = std::move(value);
    this->emit();
  }

  /**
   * @brief mutate the value in place, then notify unconditionally.
   *
   * Avoids building a whole new value and comparing it -- the point of this is
   * containers and large structs: state.mutate([](auto& v) { v.push_back(x); }).
   */
  template <typename F>
    requires std::invocable<F, T&>
  void mutate(F&& fn) {
    std::invoke(std::forward<F>(fn), this->value_);
    this->emit();
  }

  state& operator=(const T& value)
    requires std::assignable_from<T&, const T&>
  {
    set(value);
    return *this;
  }

  state& operator=(T&& value)
    requires std::assignable_from<T&, T>
  {
    set(std::move(value));
    return *this;
  }

private:
  [[no_unique_address]] Equal equal_{};
};

/**
 * @brief Derived value. Dependencies are discovered by running the compute
 *        function once and recording every observable::get() it performs.
 *
 * No dependency list to keep in sync -- adding a get() to the lambda is enough.
 * There is deliberately no set(): a derived value has exactly one source.
 * T must accept assignment from a newly computed T because every recompute
 * replaces the current value.
 *
 * Binding is deferred rather than done in the constructor so that member
 * declaration order inside a model struct does not become load-bearing.
 */
template <typename T, typename Equal = std::equal_to<>>
  requires std::predicate<Equal&, const T&, const T&> &&
           std::assignable_from<T&, T>
class computed final : public observable<T>, private detail::dependency_tracker {
public:
  computed()
    requires std::default_initializable<T> &&
             std::default_initializable<Equal>
  = default;

  explicit computed(Equal equal)
    requires std::default_initializable<T>
      : equal_(std::move(equal)) {}

  template <typename... Args>
    requires std::default_initializable<Equal> &&
             std::constructible_from<T, Args...>
  explicit computed(std::in_place_t, Args&&... args)
      : observable<T>(std::in_place, std::forward<Args>(args)...) {}

  template <typename... Args>
    requires std::constructible_from<T, Args...>
  computed(Equal equal, std::in_place_t, Args&&... args)
      : observable<T>(std::in_place, std::forward<Args>(args)...),
        equal_(std::move(equal)) {}

  /**
   * @brief set the compute function and evaluate it immediately.
   *
   * May be called only once for a computed object. Rebinding would retain
   * subscriptions discovered by the previous compute function.
   *
   * Call this after every dependency has been constructed -- typically in
   * DoInit(), not in a member initializer.
   */
  template <typename F>
    requires std::invocable<F> && std::convertible_to<std::invoke_result_t<F>, T>
  void bind(F&& calc) {
    assert(!bound() && "signals2::computed::bind() may only be called once");
    calc_ = std::forward<F>(calc);
    recompute();
  }

  bool bound() const { return static_cast<bool>(calc_); }

  /**
   * @brief Re-evaluate the compute function.
   *
   * Called automatically when a tracked dependency changes. Call it manually
   * when the value also depends on something outside this system -- a resource
   * string that changes on language switch, for instance.
   *
   * Reentrancy: a dependency may change while this is running -- from inside
   * calc_ itself, or from a subscriber during emit(). That triggers a nested
   * recompute(), which is allowed to run: it reads fresher inputs than we did,
   * so its result supersedes ours. The epoch check below is what makes that
   * safe -- without it, this frame would unwind and overwrite the nested
   * (newer) result with a value computed from inputs that no longer hold.
   *
   * Nothing here bounds recursion. Feedback that converges terminates on the
   * equality check. See "Cycles" in the file header -- divergent feedback is
   * a usage error, not a case this class recovers from.
   */
  void recompute() {
    if (!calc_) {
      return;
    }

    // Claim an epoch for this pass. Any nested recompute() claims a later one.
    const std::uint64_t started = ++epoch_;

    // The immediately-invoked lambda ends the tracking scope before the value
    // is stored and subscribers run. Otherwise a subscriber's get() calls would
    // be mistaken for dependencies of this compute function.
    T next = [this] {
      detail::tracking_scope scope(this);
      return static_cast<T>(calc_());
    }();

    if (epoch_ != started) {
      // A nested pass evaluated fresher inputs underneath us. Our inputs are
      // stale; committing `next` now would be a lost update. Drop it silently.
      return;
    }

    if (std::invoke(equal_, this->value_, next)) {
      return;
    }
    this->value_ = std::move(next);
    this->emit();
  }

private:
  void on_dependency_changed() override { recompute(); }

  void add_dependency(signals2::connection&& connection) override {
    deps_.push_back(std::move(connection));
  }

  /**
   * The dependency set only ever grows. When a branch in the compute function
   * stops reading a state, that subscription is kept rather than dropped, for
   * two reasons: dropping it would destroy a slot while the signal is still
   * emitting through it, and a stale dependency only costs one extra recompute,
   * which the equality check above absorbs. The set is bounded by the number of
   * states the function can ever reach.
   */
  bool try_mark_tracked(const void* dependency) override {
    if (std::find(tracked_.begin(), tracked_.end(), dependency) != tracked_.end()) {
      return false;
    }
    tracked_.push_back(dependency);
    return true;
  }

  [[no_unique_address]] Equal equal_{};
  std::function<T()> calc_;
  std::vector<signals2::connection> deps_;
  std::vector<const void*> tracked_;
  std::uint64_t epoch_ = 0;
};

}  // namespace signals2

#endif  // SIGNALS2_STATE_H_
