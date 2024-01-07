#ifndef SIGNALS_H_
#define SIGNALS_H_

#include <vector>
#include <functional>
#include <algorithm>
#include <memory>

namespace signals
{
  template<typename R, typename... T>
  class signal;

  namespace detail
  {
    struct connection_internal_base {
      virtual ~connection_internal_base() {

      }
    };

    template<typename R, typename... T>
    struct signal_detail;
    
    template<typename R, typename... T>
    using slot = std::function<R (T...)>;

    template<typename R, typename... T>
    class signal_lock {
    public:
      signal_lock() = default;

      explicit signal_lock(std::shared_ptr<signal_detail<R, T...>> signal)
        : signal(signal) {
        increment_lock();
      }

      signal_lock(const signal_lock& rhs)
        : signal(rhs.signal)
      {
        increment_lock();
      }

      signal_lock& operator=(const signal_lock& rhs) {
        if (&rhs == this) {
          return *this;
        }
        decrement_lock();
        signal = rhs.signal;
        increment_lock();
        return *this;
      }

      signal_lock(signal_lock&& rhs) noexcept
        : signal(std::move(rhs.signal))
      {

      }

      signal_lock& operator=(signal_lock&& rhs) noexcept {
        decrement_lock();
        signal = std::move(rhs.signal);
        return *this;
      }

      ~signal_lock() {
        decrement_lock();
      }

    private:
      void increment_lock() {
        if (!signal) {
          return;
        }
        if (signal->locks >= 0) {
          signal->locks += 1;
        } else {
          signal->locks -= 1;
        }
      }

      void decrement_lock() {
        if (!signal) {
          return;
        }
        assert(signal->locks != 0);
        bool dirty = (signal->locks < 0);
        signal->locks += dirty ? 1 : -1;
        if (signal->locks == 0 && dirty) {
          typename std::vector<slot<R, T...>*>::iterator it_1 = signal->connections.begin();
          typename std::vector<slot<R, T...>*>::iterator it_2 = it_1;
          while (it_1 != signal->connections.end()) {
            if (**(it_1)) {
              if (it_1 != it_2) {
                *it_2 = std::move(*it_1);
              }
              ++it_2;
            } else {
              delete *it_1;
            }
            ++it_1;
          }
          signal->connections.erase(it_2, signal->connections.end());
        }
      }

    public:
      std::shared_ptr<signal_detail<R, T...>> signal;
    };

    template<typename R, typename... T>
    class slot_const_iterator;

    template<typename R, typename... T>
    class slot_iterator {
      friend class signal<R, T...>;
      friend class slot_const_iterator<R, T...>;
    public:
      using iterator_category = std::forward_iterator_tag;
      using value_type = std::function<R(T...)>;
      using difference_type = std::ptrdiff_t;
      using pointer = std::function<R(T...)>*;
      using reference = std::function<R(T...)>&;

      slot_iterator() = default;

      slot_iterator(
        size_t index,
        signal_lock<R, T...>&& locker
      )
        : index_(index)
        , locker_(std::move(locker))
      {

      }

      reference operator*() const { return *(operator->()); }
      pointer operator->() const { return locker_.signal->connections[index_]; }
      slot_iterator& operator++() { ++index_; return *this; }
      slot_iterator operator++(int) { slot_iterator tmp = *this; ++(*this); return tmp; }
      friend bool operator== (const slot_iterator& a, const slot_iterator& b) { return a.index_ == b.index_ && a.locker_.signal == b.locker_.signal; }
      friend bool operator!= (const slot_iterator& a, const slot_iterator& b) { return !operator==(a, b); }

    private:
      size_t index_;
      signal_lock<R, T...> locker_;
    };

    template<typename R, typename... T>
    class slot_const_iterator {
      friend class signal<R, T...>;
    public:
      using iterator_category = std::forward_iterator_tag;
      using value_type = const std::function<R(T...)>;
      using difference_type = std::ptrdiff_t;
      using pointer = const std::function<R(T...)>*;
      using reference = const std::function<R(T...)>&;

      slot_const_iterator() = default;

      slot_const_iterator(
        size_t index,
        signal_lock<R, T...>&& locker
      )
        : index_(index)
        , locker_(std::move(locker))
      {

      }

      slot_const_iterator(const slot_iterator<R, T...>& non_const_it)
        : index_(non_const_it.index_)
        , locker_(non_const_it.locker_)
      {

      }

      slot_const_iterator(slot_iterator<R, T...>&& non_const_it) noexcept
        : index_(non_const_it.index_)
        , locker_(std::move(non_const_it.locker_))
      {

      }

      slot_const_iterator& operator=(const slot_iterator<R, T...>& non_const_it) {
        index_ = non_const_it.index_;
        locker_ = non_const_it.locker_;
        return *this;
      }

      slot_const_iterator& operator=(slot_iterator<R, T...>&& non_const_it) noexcept {
        index_ = non_const_it.index_;
        locker_ = std::move(non_const_it.locker_);
        return *this;
      }

      reference operator*() const { return *(operator->()); }
      pointer operator->() const { return locker_.signal->connections[index_]; }
      slot_const_iterator& operator++() { ++index_; return *this; }
      slot_const_iterator operator++(int) { slot_const_iterator tmp = *this; ++(*this); return tmp; }
      friend bool operator== (const slot_const_iterator& a, const slot_const_iterator& b) { return a.index_ == b.index_ && a.locker_.signal == b.locker_.signal; }
      friend bool operator!= (const slot_const_iterator& a, const slot_const_iterator& b) { return !operator==(a, b); }

    private:
      size_t index_;
      signal_lock<R, T...> locker_;
    };

    template<typename R, typename... T>
    struct signal_detail {
      std::vector<slot<R, T...>*> connections;
      long locks = 0;
    };

    template<typename R, typename... T>
    struct signal_slot_connection : public connection_internal_base {
      std::weak_ptr<signal_detail<R, T...>> the_signal;
      std::unique_ptr<slot<R, T...>> the_slot;
      virtual ~signal_slot_connection() {
        disconnect();
      }
      void disconnect() {
        *the_slot = nullptr;
        std::shared_ptr<signal_detail<R, T...>> signal = the_signal.lock();
        if (signal) {
          typename std::vector<slot<R, T...>*>::iterator it =
            std::find(signal->connections.begin(), signal->connections.end(), the_slot.get());
          if (signal->locks != 0) {
            signal->locks = (signal->locks) > 0 ? (-signal->locks) : (signal->locks);
            the_slot.release();
          } else {
            signal->connections.erase(it);
          }
        }
        the_slot.reset();
        the_signal.reset();
      }
    };

    template <std::size_t... Is, typename F, typename Tuple>
    auto invoke_impl(int, std::index_sequence<Is...>, F&& func, Tuple&& args)
      -> decltype(std::forward<F>(func)(std::get<Is>(std::forward<Tuple>(args))...))
    {
      return std::forward<F>(func)(std::get<Is>(std::forward<Tuple>(args))...);
    }

    template <std::size_t... Is, typename F, typename Tuple>
    decltype(auto) invoke_impl(char, std::index_sequence<Is...>, F&& func, Tuple&& args)
    {
      return invoke_impl(0
        , std::index_sequence<Is..., sizeof...(Is)>{}
        , std::forward<F>(func)
        , std::forward<Tuple>(args));
    }

    template <typename F, typename... Args>
    decltype(auto) invoke(F&& func, Args&&... args)
    {
      return invoke_impl(0
        , std::index_sequence<>{}
        , std::forward<F>(func)
        , std::forward_as_tuple(std::forward<Args>(args)...));
    }

    template<typename A, typename B, size_t... I>
    constexpr bool contains(std::index_sequence<I...>) {
      return std::conjunction_v<std::is_same<std::tuple_element_t<I, A>, std::tuple_element_t<I, B>>...>;
    }
  }

  template<int N>
  struct placeholder { static placeholder ph; };

  template<int N>
  placeholder<N> placeholder<N>::ph;

  template<class R, class T, class...Types, int... indices>
  std::function<R(Types...)> bind(T* obj, R(T::* member_fn)(Types...), std::integer_sequence<int, indices...> /*seq*/) {
    return std::bind(std::mem_fn(member_fn), obj, placeholder<indices + 1>::ph...);
  }

  template<class R, class T, class...Types>
  std::function<R(Types...)> bind(T* obj, R(T::* member_fn)(Types...)) {
    return bind(obj, member_fn, std::make_integer_sequence<int, sizeof...(Types)>());
  }

  class connection final {
  public:
    connection()
      : connection_detail_() {

    }

    explicit connection(std::unique_ptr<detail::connection_internal_base>&& connection_internal_detail)
      : connection_detail_(std::move(connection_internal_detail)) {

    }

    void disconnect() { connection_detail_.reset(); }

  private:
    std::unique_ptr<detail::connection_internal_base> connection_detail_;
  };

  template<typename R, typename... T>
  class signal final {
  public:
    using iterator = detail::slot_iterator<R, T...>;
    using const_iterator = detail::slot_const_iterator<R, T...>;

    signal() = default;

    signal(const signal&) = delete;

    signal& operator=(const signal&) = delete;

    signal(signal&& rhs) noexcept
      : signal_detail_(std::move(rhs.signal_detail_))
    {

    }

    signal& operator=(signal&& rhs) noexcept {
      signal_detail_ = std::move(rhs.signal_detail_);
      return *this;
    }

    iterator begin() {
      create_shared_block();
      return iterator(0, detail::signal_lock<R, T...>(signal_detail_));
    }

    iterator end() {
      create_shared_block();
      return iterator(signal_detail_->connections.size(), detail::signal_lock<R, T...>(signal_detail_));
    }

    const_iterator cbegin() {
      create_shared_block();
      return const_iterator(0, detail::signal_lock<R, T...>(signal_detail_));
    }

    const_iterator cend() {
      create_shared_block();
      return const_iterator(signal_detail_->connections.size(), detail::signal_lock<R, T...>(signal_detail_));
    }

    connection connect(const std::function<R (T...)>& the_function) {
      create_shared_block();
      std::unique_ptr<detail::signal_slot_connection<R, T...>> connection_detail(new detail::signal_slot_connection<R, T...>());
      std::unique_ptr<detail::slot<R, T...>> the_slot(new detail::slot<R, T...>(the_function));
      signal_detail_->connections.push_back(the_slot.get());
      connection_detail->the_signal = signal_detail_;
      connection_detail->the_slot = std::move(the_slot);
      connection result(std::move(connection_detail));
      return result;
    }

    connection connect(std::function<R(T...)>&& the_function) {
      create_shared_block();
      std::unique_ptr<detail::signal_slot_connection<R, T...>> connection_detail(new detail::signal_slot_connection<R, T...>());
      std::unique_ptr<detail::slot<R, T...>> the_slot(new detail::slot<R, T...>(std::move(the_function)));
      signal_detail_->connections.push_back(the_slot.get());
      connection_detail->the_signal = signal_detail_;
      connection_detail->the_slot = std::move(the_slot);
      connection result(std::move(connection_detail));
      return result;
    }

    template<typename C>
    connection connect(C* obj, R(C::* member_function)(T...)) {
      return connect(
        [=](T... args) -> R {
          return (obj->*member_function)(args...);
        }
      );
    }

    template<typename C, typename... V>
    connection connect(C* obj, R(C::*member_function)(V...)) {
      static_assert(sizeof...(T) >= sizeof...(V), "Cannot connect a slot that receive arguments more than signal can provide");
      static_assert(detail::contains<std::tuple<T...>, std::tuple<V...>>(std::make_index_sequence<sizeof...(V)>{}), "The function parameter signature that a slot accepts must match the signature of the signal");
      return connect([binder = bind(obj, member_function)](T... params) -> R {
        return detail::invoke(binder, params...);
      });
    }

    void operator()(T... param) {
      if (!signal_detail_) {
        return;
      }
      const_iterator it = cbegin();
      while (it != cend()) {
        if (*it) {
          (*it)(param...);
        }
        ++it;
      }
    }

  private:
    void create_shared_block() {
      if (!signal_detail_) {
        signal_detail_ = std::make_shared<detail::signal_detail<R, T...>>();
      }
    }

  private:
    std::shared_ptr<detail::signal_detail<R, T...>> signal_detail_;
  };
}

namespace std {
  template<int N>
  struct is_placeholder<signals::placeholder<N>> : std::integral_constant<int, N> { };
}

#endif // SIGNALS_H_