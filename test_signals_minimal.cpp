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

using std::function;
using signals::signal;
using signals::connection;

namespace {
  class TestClass {
  public:
    void Increment() { ++called_times_; }

    int called_times_ = 0;
  };
}

TEST_CASE("Test no arguments signal") {
  signal<void> signal_without_arguments;
  int slot_called_times = 0;
  function<void()> increment = [&slot_called_times]() {
    ++slot_called_times;
  };
  connection test_conn = signal_without_arguments.connect(increment);
  signal_without_arguments();
  CHECK(slot_called_times == 1);
  test_conn.disconnect();
  signal_without_arguments();
  CHECK(slot_called_times == 1);
  signal_without_arguments.connect(increment);
  signal_without_arguments();
  CHECK(slot_called_times == 1);
  test_conn = signal_without_arguments.connect(increment);
  connection test_conn2 = signal_without_arguments.connect(increment);
  signal_without_arguments();
  CHECK(slot_called_times == 3);
}

TEST_CASE("Test class member function for slot") {
  TestClass obj;
  signal<void> signal_no_arguments;
  connection conn = signal_no_arguments.connect(&obj, &TestClass::Increment);
  signal_no_arguments();
  CHECK(obj.called_times_ == 1);
  conn.disconnect();
  signal_no_arguments();
  CHECK(obj.called_times_ == 1);
}

TEST_CASE("Test disconnect during execution") {
  signal<void> signal_no_arguments;
  int slot_0_called_times = 0;
  connection conn_0 = signal_no_arguments.connect([&slot_0_called_times, &conn_0]() {
    ++slot_0_called_times;
    conn_0.disconnect();
  });
  signal_no_arguments();
  CHECK(slot_0_called_times == 1);
  signal_no_arguments();
  CHECK(slot_0_called_times == 1);
  /*
  int slot_1_called_times = 0;
  connection conn_1 = signal_no_arguments.connect([&slot_1_called_times, &conn_1]() {
    ++slot_1_called_times;
    });
  int slot_2_called_times = 0;
  connection conn_2 = signal_no_arguments.connect([&slot_2_called_times, &conn_2]() {
    ++slot_2_called_times;
    conn_2.disconnect();
    });
  int slot_3_called_times = 0;
  function<void()> slot_3 = [&slot_3_called_times]() {
    ++slot_3_called_times;
  };
  connection conn_3 = signal_no_arguments.connect(slot_3);
  */
}

TEST_CASE("Test memory leak") {

}

TEST_CASE("Test memory leak") {

}
