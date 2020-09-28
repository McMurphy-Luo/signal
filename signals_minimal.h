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
        typename std::list<std::shared_ptr<slot_shared_block<R, T...>>>::iterator it_slot = connections.begin();
        for (; it_slot != connections.end(); ++it_slot) {
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

    virtual ~connection() {

    }

    void disconnect() { connection_detail_->disconnect(); }

  private:
    std::shared_ptr<detail::connection_internal_base> connection_detail_;
  };

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

    signal& operator=(signal&& another) {
      signal_detail_ = std::move(another.signal_detail_);
    }

    ~signal() {
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

    void operator()(T... param) {
      (*signal_detail_)(param...);
    }

  private:
    std::shared_ptr<detail::signal_detail<R, T...>> signal_detail_;
  };
}

#endif // SIGNALS_H_