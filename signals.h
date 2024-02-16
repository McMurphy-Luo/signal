#ifndef SIGNALS_H_
#define SIGNALS_H_

#include <vector>
#include <functional>
#include <memory>
#include <tuple>

namespace signals2
{
  template<int N>
  struct placeholder { static placeholder ph; };

  template<int N>
  placeholder<N> placeholder<N>::ph;

  namespace detail
  {
    template<typename L> struct pop_front_impl {

    };

    template<template<typename...> typename T, typename T1, typename... TN> struct pop_front_impl<T<T1, TN...>>
    {
      using type = T<TN...>;
    };

    template<typename T> using pop_front = typename pop_front_impl<T>::type;

    template<typename... T> using first = typename std::tuple_element<0, std::tuple<T...>>::type;

    template<typename T> using rest = pop_front<T>;

    template<typename F, typename V = void>
    struct function_traits_impl
    {
    };

    template<typename F>
    struct function_traits_impl<F, std::void_t<decltype(&F::operator())>>
    {
    private:
      using tr = function_traits_impl<decltype(&F::operator())>;
    public:
      using return_type = typename tr::return_type;
      using argument_type = rest<typename tr::argument_type>;
    };

    template<typename R, typename... A>
    struct function_traits_impl<R(A...)>
    {
      using return_type = R;
      using argument_type = std::tuple<A...>;
    };

    template<bool is_function, typename... T>
    struct function_traits_helper;

    template<typename... T>
    struct function_traits_helper<true, T...> : function_traits_impl<first<T...>>
    {
      static_assert(sizeof... (T) == 1, "only accepts one template argument if it is a function object");
    };

    template<typename... T>
    struct function_traits_helper<false, T...> {
      using return_type = first<T...>;
      using argument_type = rest<std::tuple<T...>>;
    };

    template<typename... T>
    using function_traits = function_traits_helper<std::is_function<first<T...>>::value, T...>;

    struct connection_internal_base {
      virtual ~connection_internal_base() {

      }

      virtual bool connected() = 0;
    };

    template<typename F>
    class signal_detail;
    
    template<typename F>
    using slot2 = std::function<F>;

    template<typename F>
    class signal_lock {
    public:
      signal_lock() = default;

      void set_signal(signal_detail<F>* signal) {
        signal_ = signal;
      }

      void invalid() {
        locks_ = (locks_) > 0 ? (-locks_) : (locks_);
      }

      signal_detail<F>* signal() {
        return signal_;
      }

      bool dirty() {
        return locks_ < 0;
      }

      void increment() {
        locks_ += (locks_ >= 0) ? 1 : -1;
      }

      void decrement() {
        bool b_dirty = dirty();
        locks_ += b_dirty ? 1 : -1;
        if (locks_ != 0) {
          return;
        }
        if (!signal_) {
          delete this;
          return;
        }
        if (b_dirty) {
          signal_->compact();
        }
      }

      long locks() {
        return locks_;
      }

      bool locked() {
        return locks_ != 0;
      }

    private:
      signal_detail<F>* signal_ = nullptr;
      long locks_ = 0;
    };

    template<typename T>
    class lock_ptr {
    public:
      constexpr lock_ptr() noexcept : lock_(0)
      {
      }

      lock_ptr(T* p) : lock_(p)
      {
        if (lock_ != 0) lock_->increment();
      }

      lock_ptr(lock_ptr const& rhs) : lock_(rhs.lock_)
      {
        if (lock_ != 0) lock_->increment();
      }

      ~lock_ptr()
      {
        if (lock_ != 0) lock_->decrement();
      }

      lock_ptr(lock_ptr&& rhs) noexcept : lock_(rhs.lock_)
      {
        rhs.lock_ = 0;
      }

      lock_ptr& operator=(lock_ptr&& rhs) noexcept
      {
        lock_ptr(static_cast<lock_ptr&&>(rhs)).swap(*this);
        return *this;
      }

      lock_ptr& operator=(lock_ptr const& rhs)
      {
        lock_ptr(rhs).swap(*this);
        return *this;
      }

      lock_ptr& operator=(T* rhs)
      {
        lock_ptr(rhs).swap(*this);
        return *this;
      }

      T* get() const noexcept
      {
        return lock_;
      }

      T& operator*() const noexcept
      {
        assert(lock_ != 0);
        return *lock_;
      }

      T* operator->() const noexcept
      {
        assert(lock_ != 0);
        return lock_;
      }

      // implicit conversion to "bool"
      explicit operator bool() const noexcept
      {
        return lock_ != 0;
      }

      bool operator! () const noexcept
      {
        return lock_ == 0;
      }

      void swap(lock_ptr& rhs) noexcept
      {
        T* tmp = lock_;
        lock_ = rhs.lock_;
        rhs.lock_ = tmp;
      }

    private:
      T* lock_;
    };

    template<class T, class U> inline bool operator==(lock_ptr<T> const& a, lock_ptr<U> const& b) noexcept
    {
      return a.get() == b.get();
    }

    template<class T, class U> inline bool operator!=(lock_ptr<T> const& a, lock_ptr<U> const& b) noexcept
    {
      return a.get() != b.get();
    }

    template<typename F>
    class slot_const_iterator;

    template<typename F>
    class slot_iterator {
      friend class slot_const_iterator<F>;
    public:
      using iterator_category = std::forward_iterator_tag;
      using value_type = std::function<F>;
      using difference_type = std::ptrdiff_t;
      using pointer = std::function<F>*;
      using reference = std::function<F>&;

      slot_iterator() = default;

      slot_iterator(size_t index, lock_ptr<signal_lock<F>>&& lock)
        : index_(index)
        , lock_(std::move(lock))
      {

      }

      reference operator*() const { return *(operator->()); }
      pointer operator->() const { return lock_->signal()->connections()[index_]; }
      slot_iterator& operator++() { ++index_; return *this; }
      slot_iterator operator++(int) { slot_iterator tmp = *this; ++(*this); return tmp; }
      friend bool operator== (const slot_iterator& a, const slot_iterator& b) { return a.index_ == b.index_ && a.lock_ == b.lock_; }
      friend bool operator!= (const slot_iterator& a, const slot_iterator& b) { return !operator==(a, b); }

    private:
      size_t index_ = 0;
      lock_ptr<signal_lock<F>> lock_;
    };

    template<typename F>
    class slot_const_iterator {
    public:
      using iterator_category = std::forward_iterator_tag;
      using value_type = const std::function<F>;
      using difference_type = std::ptrdiff_t;
      using pointer = const std::function<F>*;
      using reference = const std::function<F>&;

      slot_const_iterator() = default;

      slot_const_iterator(size_t index, lock_ptr<signal_lock<F>>&& lock)
        : index_(index)
        , lock_(std::move(lock))
      {

      }

      slot_const_iterator(const slot_iterator<F>& non_const_it)
        : index_(non_const_it.index_)
        , lock_(non_const_it.lock_)
      {

      }

      slot_const_iterator(slot_iterator<F>&& non_const_it) noexcept
        : index_(non_const_it.index_)
        , lock_(std::move(non_const_it.lock_))
      {

      }

      slot_const_iterator& operator=(const slot_iterator<F>& non_const_it) {
        index_ = non_const_it.index_;
        lock_ = non_const_it.lock_;
        return *this;
      }

      slot_const_iterator& operator=(slot_iterator<F>&& non_const_it) noexcept {
        index_ = non_const_it.index_;
        lock_ = std::move(non_const_it.lock_);
        return *this;
      }

      reference operator*() const { return *(operator->()); }
      pointer operator->() const { return lock_->signal()->connections()[index_]; }
      slot_const_iterator& operator++() { ++index_; return *this; }
      slot_const_iterator operator++(int) { slot_const_iterator tmp = *this; ++(*this); return tmp; }
      friend bool operator== (const slot_const_iterator& a, const slot_const_iterator& b) { return a.index_ == b.index_ && a.lock_ == b.lock_; }
      friend bool operator!= (const slot_const_iterator& a, const slot_const_iterator& b) { return !operator==(a, b); }

    private:
      size_t index_ = 0;
      lock_ptr<signal_lock<F>> lock_;
    };

    template<typename F>
    class signal_detail {
      friend class signal_lock<F>;
    public:
      using iterator = slot_iterator<F>;
      using const_iterator = slot_const_iterator<F>;

      ~signal_detail() {
        if (!lock_) {
          return;
        }
        if (0 == lock_->locks()) {
          delete lock_;
          return;
        }
        if (lock_->dirty()) {
          compact();
        }
        lock_->set_signal(nullptr);
      }

      iterator begin() {
        if (!lock_) {
          lock_ = new signal_lock<F>();
          lock_->set_signal(this);
        }
        return iterator(0, lock_);
      }

      iterator end() {
        if (!lock_) {
          lock_ = new signal_lock<F>();
          lock_->set_signal(this);
        }
        return iterator(connections_.size(), lock_);
      }

      const_iterator cbegin() {
        if (!lock_) {
          lock_ = new signal_lock<F>();
          lock_->set_signal(this);
        }
        return const_iterator(0, lock_);
      }

      const_iterator cend() {
        if (!lock_) {
          lock_ = new signal_lock<F>();
          lock_->set_signal(this);
        }
        return const_iterator(connections_.size(), lock_);
      }

      std::vector<slot2<F>*>& connections() {
        return connections_;
      }

      void connect(slot2<F>* s) {
        connections_.push_back(s);
      }

      void invalid() {
        lock_->invalid();
      }

      void remove(slot2<F>* v) {
        connections_.erase(std::find(connections_.begin(), connections_.end(), v));
      }

      bool locked() {
        if (!lock_) {
          return false;
        }
        return lock_->locked();
      }

      void compact() {
        typename std::vector<slot2<F>*>::iterator it_1 = connections_.begin();
        typename std::vector<slot2<F>*>::iterator it_2 = it_1;
        while (it_1 != connections_.end()) {
          if (**(it_1)) {
            if (it_1 != it_2) {
              *it_2 = std::move(*it_1);
            }
            ++it_2;
          } else {
            delete* it_1;
          }
          ++it_1;
        }
        connections_.erase(it_2, connections_.end());
      }

    private:
      std::vector<slot2<F>*> connections_;
      signal_lock<F>* lock_ = nullptr;
    };

    template<typename F>
    struct signal_slot_connection : public connection_internal_base {
      std::weak_ptr<signal_detail<F>> the_signal;
      std::unique_ptr<slot2<F>> the_slot;
      virtual ~signal_slot_connection() override {
        disconnect();
      }

      bool connected() override {
        return !the_signal.expired();
      }

      void disconnect() {
        *the_slot = nullptr;
        std::shared_ptr<signal_detail<F>> signal = the_signal.lock();
        if (signal) {
          if (signal->locked()) {
            signal->invalid();
            the_slot.release();
          } else {
            signal->remove(the_slot.get());
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

    // https://en.cppreference.com/w/cpp/types/conditional 
    template<class...> struct conjunction : std::true_type {};
    template<class B1> struct conjunction<B1> : B1 {};
    template<class B1, class... Bn>
    struct conjunction<B1, Bn...>
      : std::conditional<bool(B1::value), conjunction<Bn...>, B1>::type {};

    template<typename A, typename B, size_t... I>
    constexpr bool contains(std::index_sequence<I...>) {
      return conjunction<std::is_same<typename std::tuple_element<I, A>::type, typename std::tuple_element<I, B>::type>...>::value;
    }

    template<class R, class T, class...Types, int... indices>
    std::function<R(Types...)> bind(T* obj, R(T::* member_fn)(Types...), std::integer_sequence<int, indices...> /*seq*/) {
      return std::bind(std::mem_fn(member_fn), obj, placeholder<indices + 1>::ph...);
    }

    template<class R, class T, class...Types>
    std::function<R(Types...)> bind(T* obj, R(T::* member_fn)(Types...)) {
      return bind(obj, member_fn, std::make_integer_sequence<int, sizeof...(Types)>());
    }
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

    bool connected() { return connection_detail_ && connection_detail_->connected(); }

  private:
    std::unique_ptr<detail::connection_internal_base> connection_detail_;
  };

  template<typename R, typename... A>
  class signal_impl {
    using function_type = R(A...);
  public:
    using iterator = typename detail::signal_detail<function_type>::iterator;
    using const_iterator = typename detail::signal_detail<function_type>::const_iterator;

    signal_impl() = default;

    signal_impl(const signal_impl&) = delete;

    signal_impl& operator=(const signal_impl&) = delete;

    signal_impl(signal_impl&& rhs) noexcept
      : signal_detail_(std::move(rhs.signal_detail_))
    {

    }

    signal_impl& operator=(signal_impl&& rhs) noexcept {
      signal_detail_ = std::move(rhs.signal_detail_);
      return *this;
    }

    iterator begin() {
      create_shared_block();
      return signal_detail_->begin();
    }

    iterator end() {
      create_shared_block();
      return signal_detail_->end();
    }

    const_iterator cbegin() {
      create_shared_block();
      return signal_detail_->cbegin();
    }

    const_iterator cend() {
      create_shared_block();
      return signal_detail_->cend();
    }

    connection connect(const std::function<function_type>& the_function) {
      create_shared_block();
      std::unique_ptr<detail::signal_slot_connection<function_type>> connection_detail(new detail::signal_slot_connection<function_type>());
      std::unique_ptr<detail::slot2<function_type>> the_slot(new detail::slot2<function_type>(the_function));
      signal_detail_->connect(the_slot.get());
      connection_detail->the_signal = signal_detail_;
      connection_detail->the_slot = std::move(the_slot);
      connection result(std::move(connection_detail));
      return result;
    }

    connection connect(std::function<function_type>&& the_function) {
      create_shared_block();
      std::unique_ptr<detail::signal_slot_connection<function_type>> connection_detail(new detail::signal_slot_connection<function_type>());
      std::unique_ptr<detail::slot2<function_type>> the_slot(new detail::slot2<function_type>(std::move(the_function)));
      signal_detail_->connect(the_slot.get());
      connection_detail->the_signal = signal_detail_;
      connection_detail->the_slot = std::move(the_slot);
      connection result(std::move(connection_detail));
      return result;
    }

    template<typename C>
    connection connect(C* obj, R(C::* member_function)(A...)) {
      return connect(
        [=](A... args) -> R {
          return (obj->*member_function)(args...);
        }
      );
    }

    template<typename C, typename... V>
    connection connect(C* obj, R(C::* member_function)(V...)) {
      static_assert(sizeof...(A) >= sizeof...(V), "cannot connect a slot that receive arguments more than signal can provide");
      static_assert(detail::contains<std::tuple<A...>, std::tuple<V...>>(std::make_index_sequence<sizeof...(V)>{}), "the signature of slot must match the signature of the signal");
      return connect([binder = detail::bind(obj, member_function)](A... params) -> R {
        return detail::invoke(binder, params...);
        });
    }

    void operator()(A... param) {
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
        signal_detail_ = std::make_shared<detail::signal_detail<function_type>>();
      }
    }

    std::shared_ptr<detail::signal_detail<function_type>> signal_detail_;
  };

  template<typename R, typename A>
  class signal_helper {

  };

  template<template<typename...> typename T, typename R, typename... A>
  class signal_helper<R, T<A...>> : public signal_impl<R, A...>
  {
  };

  template<typename... T>
  class signal2 final : public signal_helper<typename detail::function_traits<T...>::return_type, typename detail::function_traits<T...>::argument_type> {

  };
}

namespace std {
  template<int N>
  struct is_placeholder<signals2::placeholder<N>> : std::integral_constant<int, N> { };
}

#endif // SIGNALS_H_
