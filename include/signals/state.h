/**
 * @file state.h
 * @brief Lightweight observable state (State) and derived state (Computed).
 *
 * Built on top of signals2 (signals.h). Header-only, no runtime registry, no
 * singleton, no virtual dispatch on the hot path. A State is a self-contained
 * object: declare one anywhere, exactly like a signal.
 *
 * Public surface:
 *   Observable<T>  read-only interface; use as a parameter type
 *   State<T>       writable source of truth
 *   Computed<T>    read-only derived value, dependencies discovered automatically
 *   FireNow        whether Subscribe() invokes the callback once immediately
 *
 * Example:
 *   struct Model {
 *     State<float>       raw_quality;   // source
 *     Computed<CString>  quality_text;  // derived
 *   };
 *
 *   // in DoInit()
 *   model_.quality_text.Bind([this] { return FormatQuality(model_.raw_quality.Get()); });
 *   conns_.push_back(model_.quality_text.Subscribe(label_, &CLabelUI::SetText, FireNow::Yes));
 *
 *   // anywhere
 *   model_.raw_quality.Set(4.5f);       // quality_text recomputes, label_ refreshes
 *
 * Lifetime: a Subscribe() call returns a connection; whoever owns the callback
 * target must own that connection. Store them in a plain
 * std::vector<signals2::connection> member -- connection is move-only, so the
 * vector is non-copyable, and everything disconnects on destruction. Never
 * store a connection in the object being observed -- that ties the
 * subscription to the wrong lifetime.
 *
 * Threading: not thread-safe, like signals2 itself. Use from a single thread
 * (in practice, the UI thread).
 *
 * Cycles: a Computed whose own output feeds back into one of its dependencies
 * is a usage error. Feedback that settles is fine -- Set() and Recompute()
 * both stop once the value stops changing, so a self-correcting subscriber
 * (clamping, normalising) converges and is supported. Feedback that does not
 * settle recurses until the stack is exhausted; that includes a plain cycle
 * such as `x = seed + y; y = x`, which diverges as soon as it is bound. This
 * class does not detect that case, and deliberately does not paper over it by
 * dropping updates: dropping them yields a silently wrong value, which is far
 * harder to find than a crash at the point of the mistake.
 *
 * Known limitations:
 *
 * - No two-way binding. There is deliberately no "set without notifying", so
 *   writing back from an edit control inside its own change handler echoes to
 *   the view and resets the caret. Guard it at the call site with a flag; a
 *   silent Set() would desynchronize every Computed that depends on the value,
 *   which is worse.
 *
 * - Dependency tracking does not cross a DLL boundary when the Get() call
 *   happens inside an exported non-inline function of another module: the
 *   tracking stack is thread_local per module. Reading another module's state
 *   directly from a compute function is fine -- that Get() is instantiated in
 *   the calling module. Keep a Computed and its compute function in one module.
 */

#ifndef STATE_H_
#define STATE_H_

#include <algorithm>
#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

#include "signals.h"

namespace signals2 {

/// Whether Subscribe() invokes the callback once with the current value.
enum class FireNow { No, Yes };

namespace detail {

/**
 * @brief Sink for dependencies discovered while a compute function runs.
 *
 * Implemented by Computed. Never part of the public contract: the tracking
 * mechanism must stay replaceable.
 */
class ITracker {
public:
  virtual void OnDependencyChanged() = 0;
  virtual void AddDependency(signals2::connection&& connection) = 0;
  /// @return true if @p dependency was not tracked yet (caller should subscribe)
  virtual bool TryMarkTracked(const void* dependency) = 0;

protected:
  ~ITracker() = default;
};

/// A stack, not a single slot, so nested Computed evaluation nests correctly.
inline std::vector<ITracker*>& TrackingStack() {
  static thread_local std::vector<ITracker*> stack;
  return stack;
}

inline ITracker* CurrentTracker() {
  std::vector<ITracker*>& stack = TrackingStack();
  return stack.empty() ? nullptr : stack.back();
}

class TrackScope {
public:
  explicit TrackScope(ITracker* tracker) { TrackingStack().push_back(tracker); }
  ~TrackScope() { TrackingStack().pop_back(); }

  TrackScope(const TrackScope&) = delete;
  TrackScope& operator=(const TrackScope&) = delete;
};

}  // namespace detail

/**
 * @brief Read-only view of an observable value.
 *
 * Use `const Observable<T>&` as a parameter type to accept either a State or a
 * Computed without caring which. Non-copyable and non-movable: trackers and
 * slots refer to the object by address.
 */
template <typename T>
class Observable {
public:
  using ValueType = T;
  using Callback = std::function<void(const T&)>;

  Observable(const Observable&) = delete;
  Observable& operator=(const Observable&) = delete;

  /**
   * @brief Read the current value, registering a dependency if a Computed is
   *        being evaluated right now.
   */
  const T& Get() const {
    if (detail::ITracker* tracker = detail::CurrentTracker()) {
      if (tracker->TryMarkTracked(this)) {
        tracker->AddDependency(sig_.connect(
            Callback([tracker](const T&) { tracker->OnDependencyChanged(); })));
      }
    }
    return value_;
  }

  /// Read without registering a dependency. Use inside a compute function for
  /// values that should not trigger a recompute.
  const T& Peek() const { return value_; }

  /**
   * @brief Subscribe a callable taking either (const T&) or no arguments.
   * @param fire  FireNow::Yes invokes @p fn once with the current value first.
   * @return connection; the caller must keep it alive for the subscription to
   *         stay alive. Marked [[nodiscard]] -- dropping it is always a bug.
   *
   * const because `const Observable<T>&` is the read-only handle: a parameter
   * of that type must be able to observe, not just read once.
   */
  template <typename F>
    requires std::invocable<F, const T&> || std::invocable<F>
  [[nodiscard]] signals2::connection Subscribe(F&& fn, FireNow fire = FireNow::No) const {
    // Only meaningful for std::function / function pointers; skipped for lambdas.
    if constexpr (requires { static_cast<bool>(fn); }) {
      if (!fn) {
        return signals2::connection();
      }
    }

    Callback callback;
    if constexpr (std::invocable<F, const T&>) {
      callback = [f = std::forward<F>(fn)](const T& value) { f(value); };
    } else {
      callback = [f = std::forward<F>(fn)](const T&) { f(); };
    }

    if (fire == FireNow::Yes) {
      callback(value_);
    }
    return sig_.connect(std::move(callback));
  }

  /**
   * @brief Subscribe a member function.
   *
   * One overload covers every parameter type the value converts to:
   * void(const T&), void(T), and e.g. void(LPCTSTR) when T is CString and the
   * conversion is implicit. Anything else: pass a lambda.
   */
  template <typename C, typename M>
    requires std::is_member_function_pointer_v<M>
  [[nodiscard]] signals2::connection Subscribe(C* instance, M method,
                                               FireNow fire = FireNow::No) const {
    if (!instance || !method) {
      return signals2::connection();
    }
    return Subscribe([instance, method](const T& value) { (instance->*method)(value); },
                     fire);
  }

  /// Notify subscribers with the current value without changing it. Rarely
  /// needed -- prefer FireNow::Yes at subscription time.
  void Notify() { Emit(); }

protected:
  Observable() = default;
  explicit Observable(T init) : value_(std::move(init)) {}
  ~Observable() = default;

  void Emit() { sig_(value_); }

  T value_{};
  mutable signals2::signal2<void(const T&)> sig_;
};

/**
 * @brief Writable observable value.
 *
 * Set() notifies only when the value actually changes, and only when T is
 * equality comparable -- unlike a hard-coded operator!=, a T without == still
 * compiles and simply notifies every time.
 */
template <typename T>
class State final : public Observable<T> {
public:
  State() = default;
  explicit State(T init) : Observable<T>(std::move(init)) {}

  void Set(const T& value) {
    if constexpr (std::equality_comparable<T>) {
      if (this->value_ == value) {
        return;
      }
    }
    this->value_ = value;
    this->Emit();
  }

  void Set(T&& value) {
    if constexpr (std::equality_comparable<T>) {
      if (this->value_ == value) {
        return;
      }
    }
    this->value_ = std::move(value);
    this->Emit();
  }

  /**
   * @brief Mutate the value in place, then notify unconditionally.
   *
   * Avoids building a whole new value and comparing it -- the point of this is
   * containers and large structs: state.Mutate([](auto& v) { v.push_back(x); }).
   */
  template <typename F>
    requires std::invocable<F, T&>
  void Mutate(F&& mutate) {
    mutate(this->value_);
    this->Emit();
  }

  State& operator=(const T& value) {
    Set(value);
    return *this;
  }

  State& operator=(T&& value) {
    Set(std::move(value));
    return *this;
  }
};

/**
 * @brief Derived value. Dependencies are discovered by running the compute
 *        function once and recording every Observable::Get() it performs.
 *
 * No dependency list to keep in sync -- adding a Get() to the lambda is enough.
 * There is deliberately no Set(): a derived value has exactly one source.
 *
 * Binding is deferred rather than done in the constructor so that member
 * declaration order inside a model struct does not become load-bearing.
 */
template <typename T>
class Computed final : public Observable<T>, private detail::ITracker {
public:
  Computed() = default;

  /**
   * @brief Set the compute function and evaluate it immediately.
   *
   * Call this after every dependency has been constructed -- typically in
   * DoInit(), not in a member initializer.
   */
  template <typename F>
    requires std::invocable<F> && std::convertible_to<std::invoke_result_t<F>, T>
  void Bind(F&& calc) {
    calc_ = std::forward<F>(calc);
    Recompute();
  }

  bool IsBound() const { return static_cast<bool>(calc_); }

  /**
   * @brief Re-evaluate the compute function.
   *
   * Called automatically when a tracked dependency changes. Call it manually
   * when the value also depends on something outside this system -- a resource
   * string that changes on language switch, for instance.
   *
   * Reentrancy: a dependency may change while this is running -- from inside
   * calc_ itself, or from a subscriber during Emit(). That triggers a nested
   * Recompute(), which is allowed to run: it reads fresher inputs than we did,
   * so its result supersedes ours. The epoch check below is what makes that
   * safe -- without it, this frame would unwind and overwrite the nested
   * (newer) result with a value computed from inputs that no longer hold.
   *
   * Nothing here bounds the recursion. Feedback that converges terminates on
   * the equality check; feedback that does not converge exhausts the stack.
   * See "Cycles" in the file header -- that is a usage error, not a case this
   * class recovers from.
   */
  void Recompute() {
    if (!calc_) {
      return;
    }
    // Claim an epoch for this pass. Any nested Recompute() claims a later one.
    const unsigned long long started = ++epoch_;

    // The immediately-invoked lambda ends the tracking scope before the value
    // is stored and subscribers run. Otherwise a subscriber's Get() calls would
    // be mistaken for dependencies of this compute function.
    T next = [this] {
      detail::TrackScope scope(this);
      return static_cast<T>(calc_());
    }();

    if (epoch_ != started) {
      // A nested pass ran to completion underneath us. Our inputs are stale;
      // committing `next` now would be a lost update. Drop it silently -- the
      // nested pass already stored the value and notified.
      return;
    }

    if constexpr (std::equality_comparable<T>) {
      if (this->value_ == next) {
        return;
      }
    }
    this->value_ = std::move(next);
    this->Emit();
  }

private:
  void OnDependencyChanged() override { Recompute(); }

  void AddDependency(signals2::connection&& connection) override {
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
  bool TryMarkTracked(const void* dependency) override {
    if (std::find(tracked_.begin(), tracked_.end(), dependency) != tracked_.end()) {
      return false;
    }
    tracked_.push_back(dependency);
    return true;
  }

  std::function<T()> calc_;
  std::vector<signals2::connection> deps_;
  std::vector<const void*> tracked_;
  unsigned long long epoch_ = 0;
};

}  // namespace signals2

#endif  // STATE_H_
