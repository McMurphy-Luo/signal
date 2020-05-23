//
// Created by luojiayi on 5/18/17.
//

#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#include <cassert>
#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
// Replace _NORMAL_BLOCK with _CLIENT_BLOCK if you want the
// allocations to be of _CLIENT_BLOCK type
#endif // _DEBUG

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"
#include "signals.h"
#include <cstdio>
#include <vector>
#include <sstream>

using std::string;
using std::function;
using std::pair;
using std::make_pair;
using signals::signal;
using signals::connection;
using std::mem_fn;
using std::ostringstream;
using std::bind;
using std::stringstream;
using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;
using std::vector;

void Test_SimpleAssign(int input, int& output, int& called_times) {
  output = input;
  ++called_times;
}

void Test_SimplePlusOne(int input, int& output, int& called_times) {
  output = input + 1;
  ++called_times;
}

void Test_Multiple_Arguments(int simple_input, string& string_output, int& called_times) {
  ostringstream string_stream;
  string_stream << simple_input;
  string_output = string_stream.str();
  ++called_times;
}

TEST_CASE("SimpleTest") {
  signal<void, int, int&, int&> sig;
  connection temp_connect = sig.connect(&Test_SimpleAssign);
  int called_times = 0;
  int output = 0;
  sig(1, output, called_times);
  CHECK(output == 1);
  CHECK(called_times == 1);
}

TEST_CASE("ConnectDisconnectTest") {
  signal<void, int, int&, int&> sig;
  signal<void, int, int&, int&> sig2;
  connection temp_connect = sig.connect(&Test_SimpleAssign);
  int output = 0;
  int called_times = 0;
  sig(4, output, called_times);
  CHECK(called_times == 1);
  CHECK(output == 4);
  temp_connect.disconnect();
  sig(3, output, called_times);
  CHECK(called_times == 1);
  CHECK(output == 4);
  temp_connect = sig.connect(&Test_SimpleAssign);
  sig(5, output, called_times);
  CHECK(output == 5);
  CHECK(called_times == 2);
}

TEST_CASE("SignalConnectSignalTest") {
  signal<void, int, int&, int&> sig_1;
  sig_1.connect(&Test_SimpleAssign);
  int output = 0;
  int called_times = 0;
  sig_1(5, output, called_times);
  CHECK(called_times == 0);
  signal<void, int, int&, int&> sig_2;
  connection conn_sig_slot_1 = sig_1.connect(&Test_SimpleAssign);
  connection conn_sig_1_sig_2 = sig_1.connect(sig_2);
  sig_2.connect(&Test_SimpleAssign);
  sig_1(5, output, called_times);
  CHECK(called_times == 1);
  CHECK(output == 5);
  connection conn_sig_2_slot_1 = sig_2.connect(&Test_SimpleAssign);
  sig_1(6, output, called_times);
  CHECK(called_times == 3);
  CHECK(output == 6);
  conn_sig_1_sig_2.disconnect();
  sig_1(7, output, called_times);
  CHECK(called_times == 4);
  assert(output == 7);
  sig_2(8, output, called_times);
  CHECK(called_times == 5);
  CHECK(output == 8);
}

TEST_CASE("TestMultipleSlots") {
  signal<void, int, int&, int&> sig_1;
  sig_1.connect(Test_SimpleAssign);
  sig_1.connect(Test_SimplePlusOne);

  int called_times = 0;
  int output = 0;
  sig_1(123, output, called_times);
  CHECK(called_times == 0);
  connection conn_sig_1_slot_1 = sig_1.connect(Test_SimpleAssign);
  connection conn_sig_1_slot_2 = sig_1.connect(Test_SimplePlusOne);
  sig_1(5, output, called_times);
  assert(called_times == 2);
  assert(output == 6);
  sig_1(9999, output, called_times);
  assert(output == 10000);
  assert(called_times == 4);
}

class TestSignalClassMember {
public:
  TestSignalClassMember(){
    
  }
  signal<void, int, string&, int&> sig_1;
  signal<void, int, string&, int&> sig_2;
};

class TestConnectionClassMember {
public:
  TestConnectionClassMember()
    : sig()
    , the_constructor_connection(sig.connect(bind(mem_fn(&TestConnectionClassMember::TestSlot), this, _1, _2, _3)))
    , the_connection_1()
    , the_connection_2() {
  }

  void DisconnectConstructorConnection() {
    the_constructor_connection.disconnect();
  }

  void Connect1() {
    the_connection_1 = sig.connect(bind(mem_fn(&TestConnectionClassMember::TestSlot), this, _1, _2, _3));
  }

  void Connect2() {
    the_connection_2 = sig.connect(bind(mem_fn(&TestConnectionClassMember::TestSlot), this, _1, _2, _3));
  }

  void Disconnect1() {
    the_connection_1.disconnect();
  }

  void Disconnect2() {
    the_connection_2.disconnect();
  }

  void TestSlot(int input, string& output, int& called_times) {
    stringstream stream;
    stream << input * 2;
    output = stream.str();
    ++called_times;
  }

  signal<void, int, string&, int&> sig;
  connection the_constructor_connection;
  connection the_connection_1;
  connection the_connection_2;
};

TEST_CASE("TestSignalAsClassMember") {
  TestSignalClassMember* signal_class_object_1 = new TestSignalClassMember;
  TestSignalClassMember* signal_class_object_2 = new TestSignalClassMember;
  TestSignalClassMember* signal_class_object_3 = new TestSignalClassMember;

  connection test_conn_1 = signal_class_object_1->sig_1.connect(signal_class_object_1->sig_2);
  connection test_conn_2 = signal_class_object_1->sig_2.connect(signal_class_object_2->sig_1);
  connection test_conn_3 = signal_class_object_2->sig_1.connect(signal_class_object_2->sig_2);
  connection test_conn_4 = signal_class_object_2->sig_2.connect(signal_class_object_3->sig_1);
  connection test_conn_5 = signal_class_object_3->sig_1.connect(signal_class_object_3->sig_2);
  connection test_conn_6 = signal_class_object_3->sig_2.connect(Test_Multiple_Arguments);

  string output;
  int called_times = 0;
  signal_class_object_1->sig_1(100, output, called_times);
  CHECK(called_times == 1);
  CHECK(output == "100");
  signal_class_object_2->sig_1(200, output, called_times);
  CHECK(called_times == 2);
  CHECK(output == "200");
  delete signal_class_object_2;
  signal_class_object_1->sig_1(300, output, called_times);
  CHECK(output == "200");
  CHECK(called_times == 2);
  delete signal_class_object_1;
  delete signal_class_object_3;
}

TEST_CASE("TestConnectionAsClassMember") {
  TestConnectionClassMember test_object;
  string output;
  int called_times = 0;
  test_object.sig(100, output, called_times);
  CHECK(output == "200");
  CHECK(called_times == 1);
  test_object.Connect1();
  test_object.sig(200, output, called_times);
  CHECK(output == "400");
  CHECK(called_times == 3);
  test_object.DisconnectConstructorConnection();
  test_object.Connect2();
  test_object.sig(300, output, called_times);
  CHECK(output == "600");
  CHECK(called_times == 5);
  test_object.Disconnect1();
  test_object.sig(400, output, called_times);
  CHECK(output == "800");
  CHECK(called_times == 6);
}

int SlotFunctionReturnsInt(int param) {
  return param * 2;
}

class ReturnValueCollector {
public:
  ReturnValueCollector()
    : param(0)
    , return_value_collected(0)
    , called_times(0) {

  }

  bool SimpleReturnValueCollect(function<int(int)> the_slot) {
    return_value_collected = the_slot(param);
    ++called_times;
    return true;
  }

  bool ReturnValueAsInputCollect(function<int(int)> the_slot) {
    return_value_collected = the_slot(param);
    param = return_value_collected;
    ++called_times;
    return true;
  }

  bool SlotExecuteBreaker(function<int(int)> the_slot) {
    return_value_collected = the_slot(param);
    ++called_times;
    if (return_value_collected == 10) {
      return false;
    }
    param = return_value_collected;
    return true;
  }

  int param;
  int return_value_collected;
  int called_times;
};

int Test_SimplePlus2(int input) {
  return input + 2;
}

int Test_SimpleMinus2(int input) {
  return input - 2;
}

int Test_SimpleMultiply2(int input) {
  return input * 2;
}

int Test_SimpleDivide2(int input) {
  return input / 2;
}

TEST_CASE("TestSlotReturnValueCollector") {
  signal<int, int> the_signal;
  connection signal_slot_connection = the_signal.connect(SlotFunctionReturnsInt);
  ReturnValueCollector return_value_collector;
  return_value_collector.param = 10;
  the_signal(bind(mem_fn(&ReturnValueCollector::SimpleReturnValueCollect), &return_value_collector, _1));
  CHECK(return_value_collector.return_value_collected == 20);
  CHECK(return_value_collector.called_times == 1);

  signal_slot_connection = the_signal.connect(Test_SimplePlus2);
  connection signal_slot_connection2 = the_signal.connect(Test_SimpleMultiply2);
  connection signal_slot_connection3 = the_signal.connect(Test_SimpleDivide2);
  connection signal_slot_connection4 = the_signal.connect(Test_SimpleMinus2);
  return_value_collector.param = 10;
  return_value_collector.return_value_collected = 0;
  return_value_collector.called_times = 0;
  the_signal(bind(mem_fn(&ReturnValueCollector::ReturnValueAsInputCollect), &return_value_collector, _1));
  CHECK(return_value_collector.return_value_collected == 10);
  CHECK(return_value_collector.called_times == 4);
}

TEST_CASE("TestSlotExecutingBreaked") {
  signal<int, int> the_signal;

  vector<connection> connections;
  connections.push_back(the_signal.connect(Test_SimplePlus2));
  connections.push_back(the_signal.connect(Test_SimplePlus2));
  connections.push_back(the_signal.connect(Test_SimplePlus2));
  connections.push_back(the_signal.connect(Test_SimplePlus2));
  connections.push_back(the_signal.connect(Test_SimplePlus2));
  connections.push_back(the_signal.connect(Test_SimplePlus2));
  connections.push_back(the_signal.connect(Test_SimplePlus2));
  connections.push_back(the_signal.connect(Test_SimplePlus2));

  ReturnValueCollector return_value_collector;
  return_value_collector.param = 2;
  the_signal(bind(mem_fn(&ReturnValueCollector::SlotExecuteBreaker), &return_value_collector, _1));
  CHECK(return_value_collector.called_times == 4);
  CHECK(return_value_collector.return_value_collected == 10);

  return_value_collector.called_times = 0;
  return_value_collector.param = 0;
  return_value_collector.return_value_collected = 0;
  connections.clear();
  the_signal(bind(mem_fn(&ReturnValueCollector::SlotExecuteBreaker), &return_value_collector, _1));
  CHECK(return_value_collector.called_times == 0);
  connections.push_back(the_signal.connect(Test_SimplePlus2));
  return_value_collector.param = 4;
  the_signal(bind(mem_fn(&ReturnValueCollector::SlotExecuteBreaker), &return_value_collector, _1));
  CHECK(return_value_collector.called_times == 1);
  CHECK(return_value_collector.return_value_collected == 6);
}

int main(int argc, char** argv) {
  printf("Running main() from %s\n", __FILE__);

  int flag = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
  flag |= _CRTDBG_LEAK_CHECK_DF;
  flag |= _CRTDBG_ALLOC_MEM_DF;
  _CrtSetDbgFlag(flag);
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
  _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
  _CrtSetBreakAlloc(-1);
  int result = Catch::Session().run(argc, argv);
}
