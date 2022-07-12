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
    struct signal_shared_block {
      std::weak_ptr<signal_detail<R, T...>> caller;
      std::weak_ptr<signal_detail<R, T...>> callee;
    };

    template<typename R, typename... T>
    struct slot_shared_block {
      std::function<R (T...)> the_function;
    };

    template<typename R, typename... T>
    void Disconnect(std::shared_ptr<signal_shared_block<R, T...>> shared_block);

    template<typename R, typename... T>
    struct signal_detail {
      std::list<std::shared_ptr<slot_shared_block<R, T...>>> connections;
      std::list<std::shared_ptr<signal_shared_block<R, T...>>> signals_connected_to_me;
      std::list<std::shared_ptr<signal_shared_block<R, T...>>> connected_signals;

      ~signal_detail() {
        
      }

      void operator()(T... param) {
        typename std::list<std::shared_ptr<slot_shared_block<R, T...>>>::iterator it_slot = connections.begin();
        for (; it_slot != connections.end(); ++it_slot) {
          ((*it_slot)->the_function)(param...);
        }
        typename std::list<std::shared_ptr<signal_shared_block<R, T...>>>::iterator it_connected_signal = connected_signals.begin();
        for (; it_connected_signal != connected_signals.end(); ++it_connected_signal) {
          (*((*it_connected_signal)->callee.lock()))(param...);
        }
      }

      bool operator()(std::function<bool(std::function<R(T...)>)> call_policy) {
        typename std::list<std::shared_ptr<slot_shared_block<R, T...>>>::iterator it_slot = connections.begin();
        bool execute_continue = true;
        for (; it_slot != connections.end(); ++it_slot) {
          execute_continue = call_policy((*it_slot)->the_function);
          if (!execute_continue) {
            return execute_continue;
          }
        }
        typename std::list<std::shared_ptr<signal_shared_block<R, T...>>>::iterator it_connected_signal = connected_signals.begin();
        for (; it_connected_signal != connected_signals.end(); ++it_connected_signal) {
          execute_continue = (*((*it_connected_signal)->callee.lock()))(call_policy);
          if (!execute_continue) {
            return execute_continue;
          }
        }
        return execute_continue;
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

    template<typename R, typename... T>
    void Disconnect(std::shared_ptr<signal_shared_block<R, T...>> shared_block) {
      std::shared_ptr<signal_detail<R, T...>> caller = shared_block->caller.lock();
      std::shared_ptr<signal_detail<R, T...>> callee = shared_block->callee.lock();
      typename std::list<std::shared_ptr<signal_shared_block<R, T...>>>::iterator it_connected_signal = caller->connected_signals.begin();
      while (it_connected_signal != caller->connected_signals.end()) {
        if ((*it_connected_signal)->callee.lock() == callee) {
          it_connected_signal = caller->connected_signals.erase(it_connected_signal);
          break;
        } else {
          ++it_connected_signal;
        }
      }
      it_connected_signal = callee->signals_connected_to_me.begin();
      while (it_connected_signal != callee->signals_connected_to_me.end()) {
        if ((*it_connected_signal)->caller.lock() == caller) {
          it_connected_signal = callee->signals_connected_to_me.erase(it_connected_signal);
          break;
        } else {
          ++it_connected_signal;
        }
      }
    }

    template<typename R, typename... T>
    struct signal_signal_connection : public connection_internal_base {
    public:
      signal_signal_connection(std::shared_ptr<signal_shared_block<R, T...>> shared_block)
        : shared_block(shared_block) {

      }

      std::shared_ptr<signal_shared_block<R, T...>> shared_block;
      virtual ~signal_signal_connection() {
        disconnect();
      }

      virtual void disconnect() override {
        if (!shared_block) {
          return;
        }
        if (!shared_block->caller.expired() && !shared_block->callee.expired()) {
          Disconnect(shared_block);
        }
        shared_block.reset();
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

    signal(signal&& another) noexcept
      : signal_detail_(std::move(another.signal_detail_)) {

    }

    signal& operator=(signal&& another) noexcept {
      signal_detail_ = std::move(another.signal_detail_);
    }

    ~signal() {
      while (!signal_detail_->signals_connected_to_me.empty()) {
        detail::Disconnect<R, T...>(*(signal_detail_->signals_connected_to_me.begin()));
      }
      while (!signal_detail_->connected_signals.empty()) {
        detail::Disconnect<R, T...>(*(signal_detail_->connected_signals.begin()));
      }
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

    connection connect(const signal<R, T...>& another) {
      std::shared_ptr<detail::signal_shared_block<R, T...>> shared_block(std::make_shared<detail::signal_shared_block<R, T...>>());
      shared_block->callee = another.signal_detail_;
      shared_block->caller = signal_detail_;
      signal_detail_->connected_signals.push_back(shared_block);
      another.signal_detail_->signals_connected_to_me.push_back(shared_block);
      std::shared_ptr<detail::signal_signal_connection<R, T...>> connection_concrete(std::make_shared<detail::signal_signal_connection<R, T...>>(shared_block));
      connection result(connection_concrete);
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

    void operator()(std::function<bool (std::function<R (T...)>)> call_policy) {
      (*signal_detail_)(call_policy);
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