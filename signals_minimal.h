#ifndef SIGNALS_H_
#define SIGNALS_H_

#include <list>
#include <functional>
#include <algorithm>
#include <memory>

namespace signals
{
  namespace detail
  {
    struct connection_internal_base {
      virtual ~connection_internal_base() {

      }

      virtual void disconnect() = 0;
    };

    template<typename R, typename... T>
    struct signal_detail;
    
    template<typename R, typename... T>
    struct slot_shared_block {
      std::function<R (T...)> the_function;
    };

    template<typename R, typename... T>
    struct iterator_detail {
      std::shared_ptr<signal_detail<R, T...>> the_signal;

      iterator_detail(std::shared_ptr<signal_detail<R, T...>> signal)
        : the_signal(signal) {
        the_signal->locked = true;
      }

      ~iterator_detail() {
        if (the_signal->dirty) {
          if (the_signal->slots) {
            typename std::list<std::shared_ptr<slot_shared_block<R, T...>>>::iterator it_slot = the_signal->slots->connections.begin();
            it_slot = the_signal->slots->connections.begin();
            while (it_slot != the_signal->slots->connections.end()) {
              if (!((*it_slot)->the_function)) {
                it_slot = the_signal->slots->connections.erase(it_slot);
              } else {
                ++it_slot;
              }
            }
          }
        }
        the_signal->dirty = false;
        the_signal->locked = false;
      }
    };

    template<typename R, typename... T>
    class slot_const_iterator;

    template<typename R, typename... T>
    class slot_iterator {
      friend class slot_const_iterator<R, T...>;
    public:
      using iterator_category = std::forward_iterator_tag;
      using value_type = std::function<R(T...)>;
      using difference_type = std::ptrdiff_t;
      using pointer = std::function<R(T...)>*;
      using reference = std::function<R(T...)>&;

      slot_iterator()
        : it_()
        , shared_block_()
      {

      }

      slot_iterator(
        typename std::list<std::shared_ptr<slot_shared_block<R, T...>>>::iterator it,
        std::shared_ptr<iterator_detail<R, T...>> shared_block
      )
        : it_(it)
        , shared_block_(shared_block)
      {

      }

      reference operator*() const { return (*it_)->the_function; }
      pointer operator->() const { return &((*it_)->the_function); }
      slot_iterator& operator++() {
        do {
          ++it_;
          if (it_ == shared_block_->the_signal->slots->connections.end()) {
            break;
          }
          if ((*it_)->the_function) {
            break;
          }
        } while (true);
        return *this;
      }
      slot_iterator operator++(int) { slot_iterator tmp = *this; ++(*this); return tmp; }
      friend bool operator== (const slot_iterator& a, const slot_iterator& b) { return a.it_ == b.it_; }
      friend bool operator!= (const slot_iterator& a, const slot_iterator& b) { return a.it_ != b.it_; }

    private:
      typename std::list<std::shared_ptr<slot_shared_block<R, T...>>>::iterator it_;
      std::shared_ptr<iterator_detail<R, T...>> shared_block_;
    };

    template<typename R, typename... T>
    class slot_const_iterator {
    public:
      using iterator_category = std::forward_iterator_tag;
      using value_type = std::function<R(T...)>;
      using difference_type = std::ptrdiff_t;
      using pointer = const std::function<R(T...)>*;
      using reference = const std::function<R(T...)>&;

      slot_const_iterator()
        : it_()
        , shared_block_()
      {
        
      }

      slot_const_iterator(
        typename std::list<std::shared_ptr<slot_shared_block<R, T...>>>::const_iterator it,
        std::shared_ptr<iterator_detail<R, T...>> shared_block
      )
        : it_(it)
        , shared_block_(shared_block)
      {

      }

      slot_const_iterator(slot_iterator<R, T...> non_const_it)
        : it_(non_const_it.it_)
        , shared_block_(non_const_it.shared_block_)
      {

      }

      reference operator*() const { return (*it_)->the_function; }
      pointer operator->() const { return &((*it_)->the_function); }
      slot_const_iterator& operator++() {
        do {
          ++it_;
          if (it_ == shared_block_->the_signal->slots->connections.end()) {
            break;
          }
          if ((*it_)->the_function) {
            break;
          }
        } while (true);
        return *this;
      }
      slot_const_iterator operator++(int) { slot_const_iterator tmp = *this; ++(*this); return tmp; }
      friend bool operator== (const slot_const_iterator& a, const slot_const_iterator& b) { return a.it_ == b.it_; }
      friend bool operator!= (const slot_const_iterator& a, const slot_const_iterator& b) { return a.it_ != b.it_; }

    private:
      typename std::list<std::shared_ptr<slot_shared_block<R, T...>>>::const_iterator it_;
      std::shared_ptr<iterator_detail<R, T...>> shared_block_;
    };

    template<typename R, typename... T>
    struct signal_block {
      std::list<std::shared_ptr<slot_shared_block<R, T...>>> connections;
    };

    template<typename R, typename... T>
    struct signal_detail {
      std::unique_ptr<signal_block<R, T...>> slots{ new signal_block<R, T...>() };
      bool locked = false;
      bool dirty = false;
    };

    template<typename R, typename... T>
    struct signal_slot_connection : public connection_internal_base {
      std::shared_ptr<signal_detail<R, T...>> the_signal;
      std::shared_ptr<slot_shared_block<R, T...>> the_shared_block;
      virtual ~signal_slot_connection() {
        disconnect();
      }

      virtual void disconnect() override {
        if (the_signal->slots) {
          if (the_signal->locked) {
            the_shared_block->the_function = nullptr;
            the_signal->dirty = true; // We mark signal dirty. Invalid function will be erased after during signal execution
          } else {
            typename std::list<std::shared_ptr<slot_shared_block<R, T...>>>::const_iterator it =
              std::find(the_signal->slots->connections.begin(), the_signal->slots->connections.end(), the_shared_block);
            if (it != the_signal->slots->connections.end()) {
              the_signal->slots->connections.erase(it);
            }
          }
        }
        the_signal.reset();
        the_shared_block.reset();
      }
    };
  }

  class connection {
  public:
    connection()
      : connection_detail_() {

    }

    connection(std::unique_ptr<detail::connection_internal_base>&& connection_internal_detail)
      : connection_detail_(std::move(connection_internal_detail)) {

    }
    
    connection(connection&& rhs) noexcept
      : connection_detail_(std::move(rhs.connection_detail_)) {
      
    }

    connection& operator=(connection&& rhs) noexcept {
      connection_detail_ = std::move(rhs.connection_detail_);
      return *this;
    }

    void disconnect() { connection_detail_.reset(); }

  private:
    std::unique_ptr<detail::connection_internal_base> connection_detail_;
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

  template<typename R, typename... T>
  class signal {
  public:
    using iterator = detail::slot_iterator<R, T...>;
    using const_iterator = detail::slot_const_iterator<R, T...>;

    signal()
      : signal_detail_(std::make_shared<detail::signal_detail<R, T...>>()) {

    }

    ~signal() {
      signal_detail_->slots.reset();
    }

    signal(const signal&) = delete;

    signal& operator=(const signal&) = delete;

    signal(signal&& another) noexcept
      : signal_detail_(std::move(another.signal_detail_))
      , iterator_detail_(std::move(another.iterator_detail_)) {

    }

    signal& operator=(signal&& another) noexcept {
      signal_detail_ = std::move(another.signal_detail_);
      iterator_detail_ = std::move(another.iterator_detail_);
    }

    iterator begin() {
      std::shared_ptr<detail::iterator_detail<R, T...>> shared_block = iterator_detail_.lock();
      if (!shared_block) {
        shared_block = std::make_shared<detail::iterator_detail<R, T...>>(signal_detail_);
        iterator_detail_ = shared_block;
      }
      return iterator(signal_detail_->slots->connections.begin(), shared_block);
    }
    iterator end() {
      std::shared_ptr<detail::iterator_detail<R, T...>> shared_block = iterator_detail_.lock();
      if (!shared_block) {
        shared_block = std::make_shared<detail::iterator_detail<R, T...>>(signal_detail_);
        iterator_detail_ = shared_block;
      }
      return iterator(signal_detail_->slots->connections.end(), shared_block);
    }
    const_iterator cbegin() {
      std::shared_ptr<detail::iterator_detail<R, T...>> shared_block = iterator_detail_.lock();
      if (!shared_block) {
        shared_block = std::make_shared<detail::iterator_detail<R, T...>>(signal_detail_);
        iterator_detail_ = shared_block;
      }
      return const_iterator(signal_detail_->slots->connections.begin(), shared_block);
    }
    const_iterator cend() {
      std::shared_ptr<detail::iterator_detail<R, T...>> shared_block = iterator_detail_.lock();
      if (!shared_block) {
        shared_block = std::make_shared<detail::iterator_detail<R, T...>>(signal_detail_);
        iterator_detail_ = shared_block;
      }
      return const_iterator(signal_detail_->slots->connections.end(), shared_block);
    }

    connection connect(const std::function<R (T...)>& the_function) {
      std::unique_ptr<detail::signal_slot_connection<R, T...>> connection_detail(new detail::signal_slot_connection<R, T...>());
      std::shared_ptr<detail::slot_shared_block<R, T...>> the_block(std::make_shared<detail::slot_shared_block<R, T...>>());
      the_block->the_function = the_function;
      connection_detail->the_shared_block = the_block;
      connection_detail->the_signal = signal_detail_;
      signal_detail_->slots->connections.push_back(the_block);
      connection result(std::move(connection_detail));
      return result;
    }

    connection connect(std::function<R(T...)>&& the_function) {
      std::unique_ptr<detail::signal_slot_connection<R, T...>> connection_detail(new detail::signal_slot_connection<R, T...>());
      std::shared_ptr<detail::slot_shared_block<R, T...>> the_block(std::make_shared<detail::slot_shared_block<R, T...>>());
      the_block->the_function = std::move(the_function);
      connection_detail->the_shared_block = the_block;
      connection_detail->the_signal = signal_detail_;
      signal_detail_->slots->connections.push_back(the_block);
      connection result(std::move(connection_detail));
      return result;
    }

    template<typename C, typename... V>
    connection connect(C* obj, R(C::*member_function)(V...)) {
      return connect([binder = bind(obj, member_function)](T... params) -> R {
        return invoke(binder, params...);
      });
    }

    void operator()(T... param) {
      const_iterator it = begin();
      while (it != end()) {
        (*it)(param...);
        ++it;
      }
    }

  private:
    std::shared_ptr<detail::signal_detail<R, T...>> signal_detail_;
    std::weak_ptr<detail::iterator_detail<R, T...>> iterator_detail_;
  };
}

namespace std {
  template<int N>
  struct is_placeholder<signals::placeholder<N>> : std::integral_constant<int, N> { };
}

#endif // SIGNALS_H_