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
