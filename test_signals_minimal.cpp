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
#endif // _DEBUG

#include "catch.hpp"
#include "signals_minimal.h"
#include <cstdio>
#include <vector>
#include <sstream>
#include "boost/signals2.hpp"

namespace {
  class TestClass {
  public:
    void Increment() { ++called_times_; }

    int called_times_ = 0;
  };
}

TEST_CASE("Test no arguments signal") {
  signals::signal<void> signal_without_arguments;
  int slot_called_times = 0;
  std::function<void()> increment = [&slot_called_times]() {
    ++slot_called_times;
  };
  signals::connection test_conn = signal_without_arguments.connect(increment);
  signal_without_arguments();
  CHECK(slot_called_times == 1);
  test_conn.disconnect();
  signal_without_arguments();
  CHECK(slot_called_times == 1);
  signal_without_arguments.connect(increment);
  signal_without_arguments();
  CHECK(slot_called_times == 1);
  test_conn = signal_without_arguments.connect(increment);
  signals::connection test_conn2 = signal_without_arguments.connect(increment);
  signal_without_arguments();
  CHECK(slot_called_times == 3);
}

TEST_CASE("Test class member function for slot") {
  TestClass obj;
  signals::signal<void> signal_no_arguments;
  signals::connection conn = signal_no_arguments.connect(&obj, &TestClass::Increment);
  signal_no_arguments();
  CHECK(obj.called_times_ == 1);
  conn.disconnect();
  signal_no_arguments();
  CHECK(obj.called_times_ == 1);
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
  signals::signal<void> signal_no_arguments;
  int slot_0_called_times = 0;
  signals::connection conn_0 = signal_no_arguments.connect([&slot_0_called_times, &conn_0]() {
    ++slot_0_called_times;
    conn_0.disconnect();
  });
  signal_no_arguments();
  CHECK(slot_0_called_times == 1);
  signal_no_arguments();
  CHECK(slot_0_called_times == 1);
  signals::connection conn_2;
  signals::connection conn_1 = signal_no_arguments.connect([&conn_2]() {
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
  signals::signal<void> signal_no_arguments;
  int slot_0_called_times = 0;
  signals::connection conn_out = signal_no_arguments.connect([&signal_no_arguments, &conn_out]() {
    conn_out = signal_no_arguments.connect([]() {});
  });
  signal_no_arguments();
}

TEST_CASE("Test signal iterator 1") {
  signals::signal<int, int> signal_simple;
  signals::connection conn_plus_1 = signal_simple.connect([](int x) -> int {
    return x + 1;
  });
  signals::connection conn_multiply_2 = signal_simple.connect([](int x) -> int {
    return x * 2;
  });
  signals::connection conn_minus_3 = signal_simple.connect([](int x) -> int {
    return x - 3;
  });
  signals::signal<int, int>::const_iterator it = signal_simple.begin();
  CHECK((*it)(5) == 6);
  CHECK(it != signal_simple.end());
  ++it;
  CHECK((*it)(5) == 10);
  CHECK((*it--)(5) == 10);
  CHECK(it == signal_simple.begin());
  CHECK((*it)(5) == 6);
  CHECK((*it++)(5) == 6);
  CHECK((*it++)(5) == 10);
  CHECK((*it)(5) == 2);
  ++it;
  CHECK(it == signal_simple.end());
  it = signal_simple.begin();
  signals::signal<int, int> signal_simple_2;
  signals::connection conn = signal_simple_2.connect([](int x) -> int {
    return x * x;
  });
  it = signal_simple_2.begin();
  CHECK((*it)(5) == 25);
  ++it;
  CHECK(it == signal_simple_2.end());
}

TEST_CASE("Test signal iterator 2") {
  signals::signal<int, int, bool&> signal_of_interupt;
  signals::connection conn_1 = signal_of_interupt.connect([](int x, bool& handled) -> int {
    return x * 16;
  });
  signals::connection conn_2 = signal_of_interupt.connect([](int x, bool& handled) -> int {
    handled = true;
    return x - 3;
  });
  signals::connection conn_3 = signal_of_interupt.connect([](int x, bool& handled) -> int {
    return x * 2;
  });

  int init = 1;
  signals::signal<int, int, bool&>::const_iterator it = signal_of_interupt.begin();

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

TEST_CASE("Test signal iterator stl compatibility") {
  signals::signal<int, int> test_simple_signal;
  signals::connection conn = test_simple_signal.connect([](int i) { return i + 3; });
  signals::signal<int, int>::const_iterator it = test_simple_signal.begin();
  std::advance(it, 1);
  CHECK(it == test_simple_signal.end());
  CHECK(std::end(test_simple_signal) == it);
  std::advance(it, -1);
  CHECK(std::begin(test_simple_signal) == it);
  CHECK(std::end(test_simple_signal) == std::next(it));
  std::advance(it, 1);
  CHECK(std::begin(test_simple_signal) == std::prev(it));
  std::iterator_traits<signals::signal<int, int>::const_iterator>::value_type item = *(std::prev(it));
  std::iterator_traits<signals::signal<int, int>::iterator>::value_type item_2 = *(std::prev(it));
  CHECK(item(5) == 8);
  CHECK(item_2(9) == 12);
  signals::signal<int, int>::const_iterator it_1 = test_simple_signal.begin();
  signals::signal<int, int>::const_iterator it_2 = test_simple_signal.end();
  std::swap(it_1, it_2);
  CHECK(it_1 == test_simple_signal.end());
  CHECK(it_2 == test_simple_signal.begin());
}

TEST_CASE("Test disconnect during iterating") {
  signals::signal<void> test_simple_signal;
  signals::connection conn = test_simple_signal.connect([&conn] {
    conn.disconnect();
  });
  signals::connection conn_2 = test_simple_signal.connect([] {});
  signals::connection conn_3 = test_simple_signal.connect([&conn_3] {
    conn_3.disconnect();
  });
  signals::connection conn_4 = test_simple_signal.connect([] {});
  signals::connection conn_5 = test_simple_signal.connect([] {});
  signals::connection conn_6 = test_simple_signal.connect([&conn_5] { conn_5.disconnect(); });
  signals::connection conn_7 = test_simple_signal.connect([] {});
  signals::connection conn_9;
  signals::connection conn_8 = test_simple_signal.connect([&conn_9] { conn_9.disconnect(); });
  conn_9 = test_simple_signal.connect([] {});
  signals::connection conn_10 = test_simple_signal.connect([] {});

  signals::signal<void>::const_iterator it = test_simple_signal.begin();
  while (it != test_simple_signal.end()) {
    (*it)();
    ++it;
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
