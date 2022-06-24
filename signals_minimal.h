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
    struct signal_detail {
      std::list<std::shared_ptr<slot_shared_block<R, T...>>> connections;

      ~signal_detail() {

      }

      void operator()(T... param) {
        std::list<std::shared_ptr<slot_shared_block<R, T...>>> connections_copy = connections;
        typename std::list<std::shared_ptr<slot_shared_block<R, T...>>>::iterator it_slot = connections_copy.begin();
        for (; it_slot != connections_copy.end(); ++it_slot) {
          ((*it_slot)->the_function)(param...);
        }
      }
    };

    template<typename R, typename... T>
    struct signal_slot_connection : public connection_internal_base {
      std::weak_ptr<signal_detail<R, T...>> the_signal;
      std::shared_ptr<slot_shared_block<R, T...>> the_shared_block;
      virtual ~signal_slot_connection() {
        disconnect();
      }

      virtual void disconnect() override {
        std::shared_ptr<signal_detail<R, T...>> the_signal_locked = the_signal.lock();
        if (the_signal_locked) {
          typename std::list<std::shared_ptr<slot_shared_block<R, T...>>>::const_iterator it = 
            std::find(the_signal_locked->connections.begin(), the_signal_locked->connections.end(), the_shared_block);
          if (it != the_signal_locked->connections.end()) {
            the_signal_locked->connections.erase(it);
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

    connection(std::shared_ptr<detail::connection_internal_base> connection_internal_detail)
      : connection_detail_(connection_internal_detail) {

    }

    void disconnect() { if (connection_detail_) connection_detail_->disconnect(); }

  private:
    std::shared_ptr<detail::connection_internal_base> connection_detail_;
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
    signal()
      : signal_detail_(std::make_shared<detail::signal_detail<R, T...>>()) {

    }

    signal(const signal&) = delete;

    signal& operator=(signal&) = delete;

    signal(const signal&& another) noexcept
      : signal_detail_(std::move(another.signal_detail_)) {

    }

    signal& operator=(signal&& another) noexcept {
      signal_detail_ = std::move(another.signal_detail_);
    }

    connection connect(const std::function<R (T...)>& the_function) {
      std::shared_ptr<detail::signal_slot_connection<R, T...>> connection_detail(std::make_shared<detail::signal_slot_connection<R, T...>>());
      std::shared_ptr<detail::slot_shared_block<R, T...>> the_block(std::make_shared<detail::slot_shared_block<R, T...>>());
      the_block->the_function = the_function;
      connection_detail->the_shared_block = the_block;
      connection_detail->the_signal = signal_detail_;
      signal_detail_->connections.push_back(the_block);
      connection result(connection_detail);
      return result;
    }

    connection connect(std::function<R(T...)>&& the_function) {
      std::shared_ptr<detail::signal_slot_connection<R, T...>> connection_detail(std::make_shared<detail::signal_slot_connection<R, T...>>());
      std::shared_ptr<detail::slot_shared_block<R, T...>> the_block(std::make_shared<detail::slot_shared_block<R, T...>>());
      the_block->the_function = std::move(the_function);
      connection_detail->the_shared_block = the_block;
      connection_detail->the_signal = signal_detail_;
      signal_detail_->connections.push_back(the_block);
      connection result(connection_detail);
      return result;
    }

    template<typename C, typename... V>
    connection connect(C* obj, R(C::*member_function)(V...)) {
      return connect([binder = bind(obj, member_function)](T... params) -> R {
        invoke(binder, params...);
      });
    }

    void operator()(T... param) {
      (*signal_detail_)(param...);
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