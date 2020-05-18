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
#include <sstream>

using namespace signals;
using std::function;

void Test_SimpleAssign(int input, int& output, int& called_times) {
  output = input;
  ++called_times;
}

void Test_SimplePlusOne(int input, int& output, int& called_times) {
  output = input + 1;
  ++called_times;
}

void Test_Multiple_Arguments(int simple_input, std::string& string_output, int& called_times) {
  std::ostringstream string_stream;
  string_stream << simple_input;
  string_output = string_stream.str();
  ++called_times;
}

TEST_CASE("SimpleTest") {
  signals::signal<void, int, int&, int&> sig;
  signals::connection temp_connect = sig.connect(&Test_SimpleAssign);
  int called_times = 0;
  int output = 0;
  sig(1, output, called_times);
  CHECK(output == 1);
  CHECK(called_times == 1);
}

TEST_CASE("ConnectDisconnectTest") {
  signals::signal<void, int, int&, int&> sig;
  signals::signal<void, int, int&, int&> sig2;
  signals::connection temp_connect = sig.connect(&Test_SimpleAssign);
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
  signals::signal<void, int, int&, int&> sig_1;
  sig_1.connect(&Test_SimpleAssign);
  int output = 0;
  int called_times = 0;
  sig_1(5, output, called_times);
  CHECK(called_times == 0);
  signals::signal<void, int, int&, int&> sig_2;
  signals::connection conn_sig_slot_1 = sig_1.connect(&Test_SimpleAssign);
  signals::connection conn_sig_1_sig_2 = sig_1.connect(sig_2);
  sig_2.connect(&Test_SimpleAssign);
  sig_1(5, output, called_times);
  CHECK(called_times == 1);
  CHECK(output == 5);
  signals::connection conn_sig_2_slot_1 = sig_2.connect(&Test_SimpleAssign);
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
  signals::signal<void, int, int&, int&> sig_1;
  sig_1.connect(Test_SimpleAssign);
  sig_1.connect(Test_SimplePlusOne);

  int called_times = 0;
  int output = 0;
  sig_1(123, output, called_times);
  CHECK(called_times == 0);
  signals::connection conn_sig_1_slot_1 = sig_1.connect(Test_SimpleAssign);
  signals::connection conn_sig_1_slot_2 = sig_1.connect(Test_SimplePlusOne);
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
  signals::signal<void, int, std::string&, int&> sig_1;
  signals::signal<void, int, std::string&, int&> sig_2;
};

class TestConnectionClassMember {
public:
  TestConnectionClassMember()
    : sig()
    , the_constructor_connection(sig.connect(std::bind(std::mem_fn(&TestConnectionClassMember::TestSlot), this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)))
    , the_connection_1()
    , the_connection_2() {
  }

  void DisconnectConstructorConnection() {
    the_constructor_connection.disconnect();
  }

  void Connect1() {
    the_connection_1 = sig.connect(std::bind(std::mem_fn(&TestConnectionClassMember::TestSlot), this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
  }

  void Connect2() {
    the_connection_2 = sig.connect(std::bind(std::mem_fn(&TestConnectionClassMember::TestSlot), this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
  }

  void Disconnect1() {
    the_connection_1.disconnect();
  }

  void Disconnect2() {
    the_connection_2.disconnect();
  }

  void TestSlot(int input, std::string& output, int& called_times) {
    std::stringstream stream;
    stream << input * 2;
    output = stream.str();
    ++called_times;
  }

  signals::signal<void, int, std::string&, int&> sig;
  signals::connection the_constructor_connection;
  signals::connection the_connection_1;
  signals::connection the_connection_2;
};

TEST_CASE("TestSignalAsClassMember") {
  TestSignalClassMember* signal_class_object_1 = new TestSignalClassMember;
  TestSignalClassMember* signal_class_object_2 = new TestSignalClassMember;
  TestSignalClassMember* signal_class_object_3 = new TestSignalClassMember;

  signals::connection test_conn_1 = signal_class_object_1->sig_1.connect(signal_class_object_1->sig_2);
  signals::connection test_conn_2 = signal_class_object_1->sig_2.connect(signal_class_object_2->sig_1);
  signals::connection test_conn_3 = signal_class_object_2->sig_1.connect(signal_class_object_2->sig_2);
  signals::connection test_conn_4 = signal_class_object_2->sig_2.connect(signal_class_object_3->sig_1);
  signals::connection test_conn_5 = signal_class_object_3->sig_1.connect(signal_class_object_3->sig_2);
  signals::connection test_conn_6 = signal_class_object_3->sig_2.connect(Test_Multiple_Arguments);

  std::string output;
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
  std::string output;
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

class ReturnValueSum {
public:
  ReturnValueSum()
    : sum_of_return_value(0){

  }

  bool operator()(function<int(int)> the_function, int the_param) {
    sum_of_return_value += the_function(the_param);
    return true;
  }

  int sum_of_return_value;
};

TEST_CASE("TestSlotReturnValueCollector") {
  signal<int, int> the_signal;
  connection signal_slot_connection = the_signal.connect(SlotFunctionReturnsInt);
  ReturnValueSum return_value_collector;
  the_signal(std::bind(std::mem_fn(&ReturnValueSum::operator()), &return_value_collector, std::placeholders::_1, std::placeholders::_2), 5);
  CHECK(return_value_collector.sum_of_return_value == 10);
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
