/**
 * @author McMurphy Luo
 * @description Test cases for a compact version signals
 */

#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#include <cassert>
#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
// Replace _NORMAL_BLOCK with _CLIENT_BLOCK if you want the
// allocations to be of _CLIENT_BLOCK type
#define private public
#endif // _DEBUG

#include "catch.hpp"
#include "signals.h"
#include <cstdio>
#include <vector>
#include <sstream>
#include <functional>
#include "boost/signals2.hpp"

namespace {
  class TestClass {
  public:
    void SetValue(int value) { value_ = value; ++called_times_; }

    void GetValue(int& target) { target = value_; ++called_times_; }

    int value_ = 0;
    int called_times_ = 0;
  };
}

TEST_CASE("Test no arguments signal") {
  signals2::signal2<void> signal_without_arguments;
  int slot_called_times = 0;
  std::function<void()> increment = [&slot_called_times]() {
    ++slot_called_times;
  };
  signals2::connection test_conn = signal_without_arguments.connect(increment);
  signal_without_arguments();
  CHECK(slot_called_times == 1);
  test_conn.disconnect();
  signal_without_arguments();
  CHECK(slot_called_times == 1);
  signal_without_arguments.connect(increment);
  signal_without_arguments();
  CHECK(slot_called_times == 1);
  test_conn = signal_without_arguments.connect(increment);
  signals2::connection test_conn2 = signal_without_arguments.connect(increment);
  signal_without_arguments();
  CHECK(slot_called_times == 3);
}

TEST_CASE("Test class member function for slot") {
  TestClass obj;
  signals2::signal2<void, int> signal_no_arguments;
  signals2::connection conn = signal_no_arguments.connect(&obj, &TestClass::SetValue);
  signal_no_arguments(5);
  CHECK(obj.called_times_ == 1);
  CHECK(obj.value_ == 5);
  conn.disconnect();
  signal_no_arguments(4);
  CHECK(obj.called_times_ == 1);
  CHECK(obj.value_ == 5);
  conn = signal_no_arguments.connect(&obj, &TestClass::SetValue);
  signal_no_arguments(89);
  CHECK(obj.called_times_ == 2);
  CHECK(obj.value_ == 89);
  signals2::signal2<void, int&, std::string> signal_get_value;
  signals2::connection conn_2 = signal_get_value.connect(&obj, &TestClass::GetValue);
  int output = -1;
  std::string test_str("test");
  signal_get_value(output, test_str);
  CHECK(test_str == "test");
  CHECK(output == 89);
  CHECK(obj.called_times_ == 3);
}

TEST_CASE("Test boost disconnect during execution") {
  boost::signals2::signal<void()> signal_no_arguments;
  int slot_0_called_times = 0;
  boost::signals2::connection conn_0 = signal_no_arguments.connect([&slot_0_called_times, &conn_0]() {
    ++slot_0_called_times;
    conn_0.disconnect();
  });
  signal_no_arguments();
  CHECK(slot_0_called_times == 1);
  signal_no_arguments();
  CHECK(slot_0_called_times == 1);
  boost::signals2::connection conn_2;
  boost::signals2::connection conn_1 = signal_no_arguments.connect([&conn_2]() {
    conn_2.disconnect();
    });
  int slot_2_called_times = 0;
  conn_2 = signal_no_arguments.connect([&slot_2_called_times]() {
    ++slot_2_called_times;
    });
  signal_no_arguments();
  CHECK(slot_2_called_times == 0);
}

TEST_CASE("Test signal disconnect during execution") {
  signals2::signal2<void> signal_no_arguments;
  int slot_0_called_times = 0;
  signals2::connection conn_0 = signal_no_arguments.connect([&slot_0_called_times, &conn_0]() {
    ++slot_0_called_times;
    conn_0.disconnect();
  });
  signal_no_arguments();
  CHECK(slot_0_called_times == 1);
  signal_no_arguments();
  CHECK(slot_0_called_times == 1);
  signals2::connection conn_2;
  signals2::connection conn_1 = signal_no_arguments.connect([&conn_2]() {
    conn_2.disconnect();
  });
  int slot_2_called_times = 0;
  conn_2 = signal_no_arguments.connect([&slot_2_called_times]() {
    ++slot_2_called_times;
  });
  signal_no_arguments();
  CHECK(slot_2_called_times == 0);
}

TEST_CASE("Test boost connect during execution") {
  boost::signals2::signal<void()> signal_no_arguments;
  int slot_0_called_times = 0;
  boost::signals2::connection conn_out = signal_no_arguments.connect([&signal_no_arguments, &conn_out]() {
    conn_out = signal_no_arguments.connect([]() {});
  });
  signal_no_arguments();
}

TEST_CASE("Test signal connect during execution") {
  signals2::signal2<void> signal_no_arguments;
  int slot_0_called_times = 0;
  signals2::connection conn_out = signal_no_arguments.connect([&signal_no_arguments, &conn_out]() {
    conn_out = signal_no_arguments.connect([]() {});
  });
  signal_no_arguments();
}

TEST_CASE("Test signal iterator 1") {
  signals2::signal2<int, int> signal_simple;
  signals2::connection conn_plus_1 = signal_simple.connect([](int x) -> int {
    return x + 1;
  });
  signals2::connection conn_multiply_2 = signal_simple.connect([](int x) -> int {
    return x * 2;
  });
  signals2::connection conn_minus_3 = signal_simple.connect([](int x) -> int {
    return x - 3;
  });
  signals2::signal2<int, int>::const_iterator it = signal_simple.begin();
  CHECK(signal_simple.signal_detail_->lock_->locks_ == 1);
  CHECK((*it)(5) == 6);
  CHECK(it != signal_simple.end());
  ++it;
  CHECK(signal_simple.signal_detail_->lock_->locks_ == 1);
  signal_simple.begin();
  CHECK(signal_simple.signal_detail_->lock_->locks_ == 1);
  signal_simple.cbegin();
  CHECK(signal_simple.signal_detail_->lock_->locks_ == 1);
  signal_simple.cend();
  CHECK(signal_simple.signal_detail_->lock_->locks_ == 1);
  CHECK((*it)(5) == 10);
  CHECK(it != signal_simple.begin());
  CHECK(it != signal_simple.end());
  CHECK((*it++)(5) == 10);
  CHECK((*it)(5) == 2);
  ++it;
  CHECK(it == signal_simple.end());
  it = signal_simple.begin();
  signals2::signal2<int, int> signal_simple_2;
  signals2::connection conn = signal_simple_2.connect([](int x) -> int {
    return x * x;
  });
  CHECK(signal_simple_2.signal_detail_->lock_ == nullptr);
  CHECK(signal_simple.signal_detail_->lock_->locks_ == 1);
  it = signal_simple_2.begin();
  CHECK(signal_simple.signal_detail_->lock_->locks_ == 0);
  CHECK(signal_simple_2.signal_detail_->lock_->locks_ == 1);
  CHECK((*it)(5) == 25);
  ++it;
  CHECK(it == signal_simple_2.end());
}

TEST_CASE("Test signal iterator 2") {
  signals2::signal2<int, int, bool&> signal_of_interupt;
  signals2::connection conn_1 = signal_of_interupt.connect([](int x, bool& handled) -> int {
    return x * 16;
  });
  signals2::connection conn_2 = signal_of_interupt.connect([](int x, bool& handled) -> int {
    handled = true;
    return x - 3;
  });
  signals2::connection conn_3 = signal_of_interupt.connect([](int x, bool& handled) -> int {
    return x * 2;
  });

  int init = 1;
  signals2::signal2<int, int, bool&>::const_iterator it = signal_of_interupt.begin();

  while (it != signal_of_interupt.end()) {
    bool handled = false;
    init = (*it)(init, handled);
    if (handled) {
      break;
    }
    ++it;
  }
  CHECK(init == 13);
}

TEST_CASE("Test signal iteerator three") {
  int test = 5;
  {
    signals2::signal2<void, int&>::const_iterator iter;
    {
      signals2::signal2<void, int&> test_signal;
      signals2::connection conn = test_signal.connect([](int& i) { return i = i + 1; });
      iter = test_signal.begin();
      while (iter != test_signal.end()) {
        if (*iter) {
          (*iter)(test);
        } else {
          CHECK(!(iter->operator bool()));
        }
        ++iter;
      }
    }
  }
  CHECK(test == 6);
  {
    signals2::signal2<void, int&> test_signal;
    {
      signals2::signal2<void, int&>::iterator it;
      signals2::signal2<void, int&>::const_iterator const_it = it;
      signals2::connection conn = test_signal.connect([](int& i) { return i = i + 1; });
      it = test_signal.begin();
      while (it != test_signal.end()) {
        if (*it) {
          (*it)(test);
        }
        ++it;
      }
      CHECK(test_signal.signal_detail_->lock_->locks_ == 1);
      const_it = it;
      CHECK(test_signal.signal_detail_->lock_->locks_ == 2);
      CHECK(test == 7);
      CHECK(const_it == test_signal.end());
      const_it = test_signal.begin();
      while (const_it != test_signal.end()) {
        if (*const_it) {
          (*const_it)(test);
        }
        ++const_it;
      }
      CHECK(test == 8);
      CHECK(it == test_signal.end());
      CHECK(const_it == test_signal.end());
      CHECK(it == const_it);
    }
    CHECK(test_signal.signal_detail_->lock_->locks_ == 0);
  }
}

TEST_CASE("Test signal iterator stl compatibility") {
  signals2::signal2<int, int> test_simple_signal;
  signals2::connection conn = test_simple_signal.connect([](int i) { return i + 3; });
  signals2::signal2<int, int>::const_iterator it = test_simple_signal.begin();
  std::advance(it, 1);
  CHECK(it == test_simple_signal.end());
  CHECK(std::end(test_simple_signal) == it);
  signals2::signal2<int, int>::const_iterator it_1 = test_simple_signal.begin();
  signals2::signal2<int, int>::const_iterator it_2 = test_simple_signal.end();
  std::swap(it_1, it_2);
  CHECK(it_1 == test_simple_signal.end());
  CHECK(it_2 == test_simple_signal.begin());
}

TEST_CASE("Test disconnect during iterating") {
  signals2::signal2<void> test_simple_signal;
  signals2::connection conn;
  signals2::connection conn_3;
  {
    conn = test_simple_signal.connect([&conn] {
      conn.disconnect();
    });
    signals2::connection conn_1 = test_simple_signal.connect([] {});
    signals2::connection conn_2 = test_simple_signal.connect([&conn_2] {
      conn_2.disconnect();
    });
    conn_3 = test_simple_signal.connect([] {});
    signals2::connection conn_4 = test_simple_signal.connect([] {});
    signals2::connection conn_5 = test_simple_signal.connect([&conn_4] { conn_4.disconnect(); });
    signals2::connection conn_6 = test_simple_signal.connect([] {});
    signals2::connection conn_7;
    signals2::connection conn_8 = test_simple_signal.connect([&conn_7] { conn_7.disconnect(); });
    conn_7 = test_simple_signal.connect([] {});
    signals2::connection conn_9 = test_simple_signal.connect([] {});
    signals2::signal2<void>::const_iterator it = test_simple_signal.begin();
    while (it != test_simple_signal.end()) {
      if (*it) {
        (*it)();
      }
      ++it;
    }
  }
}

TEST_CASE("Test boost disconnect during iterating") {
  boost::signals2::signal<void()> test_simple_signal;
  boost::signals2::connection conn = test_simple_signal.connect([&conn] {
    conn.disconnect();
  });
  boost::signals2::connection conn_2 = test_simple_signal.connect([] {});
  boost::signals2::connection conn_3 = test_simple_signal.connect([&conn_3] {
    conn_3.disconnect();
  });
  boost::signals2::connection conn_4 = test_simple_signal.connect([] {});
  boost::signals2::connection conn_5 = test_simple_signal.connect([] {});
  boost::signals2::connection conn_6 = test_simple_signal.connect([&conn_5] { conn_5.disconnect(); });
  boost::signals2::connection conn_7 = test_simple_signal.connect([] {});
  boost::signals2::connection conn_9;
  boost::signals2::connection conn_8 = test_simple_signal.connect([&conn_9] { conn_9.disconnect(); });
  conn_9 = test_simple_signal.connect([] {});
  boost::signals2::connection conn_10 = test_simple_signal.connect([] {});
  test_simple_signal();
}

class Counter {
public:
  Counter(int* t)
    : count(t) {
    ++(*count);
  }

  ~Counter() {
    --(*count);
  }

  Counter(const Counter&) = delete;
  Counter& operator=(const Counter&) = delete;
  Counter(Counter&&) = delete;
  Counter& operator=(Counter&&) = delete;

private:
  int* count;
};

TEST_CASE("Test slot resource management and termination") {
  signals2::signal2<void> test_signal;
  int test = 0;
  signals2::connection conn = test_signal.connect([t = std::make_shared<Counter>(&test)]() {

  });
  CHECK(test == 1);
  conn.disconnect();
  CHECK(test == 0);
  conn.disconnect();
  CHECK(test == 0);
}

class Foo {
public:
  void foo(int, const long&) {
    
  }
};

TEST_CASE("Test connect class member function") {
  signals2::signal2<void, int, const long&, long> signal;
  Foo foo_instance;
  signal.connect(&foo_instance, &Foo::foo);
}

TEST_CASE("Test connect while triggering") {
  boost::signals2::signal<void(int&)> b_signal;
  boost::signals2::connection b_conn;

  std::function<void(int&)> b_lambda;
  b_lambda = [&b_signal, &b_lambda, &b_conn](int& v) {
    ++v;
    b_conn = b_signal.connect(b_lambda);
  };
  b_conn = b_signal.connect(b_lambda);
  int b = 0;
  b_signal(b);
  
  signals2::signal2<void, int&> s_signal;
  signals2::connection s_conn;
  std::function<void(int&)> s_lambda = [&s_signal, &s_lambda, &s_conn](int& v) {
    // infinite loop
    ++v;
    if (v != 99) {
      s_conn = s_signal.connect(s_lambda);
    }
  };
  s_conn = s_signal.connect(s_lambda);
  int s = 0;
  s_signal(s);
  CHECK(s == 99);
}

TEST_CASE("Test connected") {
  boost::signals2::connection conn_boost;
  {
    CHECK(!conn_boost.connected());
    boost::signals2::signal<int(int)> test_b_signal;
    conn_boost = test_b_signal.connect([](int v) {
      return v + 1;
    });
    CHECK(conn_boost.connected());
  }
  CHECK(!conn_boost.connected());

  signals2::connection conn_signal;
  {
    CHECK(!conn_signal.connected());
    signals2::signal2<int, int> test_signal;
    conn_signal = test_signal.connect([](int v) {
      return v + 1;
    });
    CHECK(conn_signal.connected());
  }
  CHECK(!conn_signal.connected());
}
