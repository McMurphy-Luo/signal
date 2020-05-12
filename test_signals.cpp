//
// Created by luojiayi on 5/18/17.
//

#include "signals.h"
#include <cstdio>
#include <sstream>

using namespace signals;

static int test_input_1_called_times = 0;
static int test_input_1_output = 0;
void Test_SimpleAssign_1(int input) {
  ++test_input_1_called_times;
  test_input_1_output = input;
}

static int test_input_2_called_times = 0;
static int test_input_2_output = 0;
void Test_SimpleAssign_2(int input) {
  ++test_input_2_called_times;
  test_input_2_output = input;
}

static int test_multiple_arguments_called_times = 0;
static int test_multiple_arguments_output_1 = 0;
void Test_Multiple_Arguments(int simple_input, std::string& reference_input) {
  ++test_multiple_arguments_called_times;
  test_multiple_arguments_output_1 = simple_input;
  std::ostringstream string_stream;
  string_stream << simple_input;
  reference_input = string_stream.str();
}

void Reset() {
  test_input_2_called_times = 0;
  test_input_2_output = 0;
  test_input_1_called_times = 0;
  test_input_1_output = 0;
}

void SimpleTest() {
  Reset();
  signals::signal<int> sig;
  signals::connection temp_connect = sig.connect(&Test_SimpleAssign_1);
  sig(1);
  assert(test_input_1_output == 1);
  assert(test_input_2_called_times = 1);
}

void ConnectDisconnectTest() {
  Reset();
  signals::signal<int> sig;
  signals::signal<int> sig2;
  signals::connection temp_connect = sig.connect(&Test_SimpleAssign_1);
  sig(4);
  assert(test_input_1_called_times == 1);
  assert(test_input_1_output == 4);
  temp_connect.disconnect();
  sig(3);
  assert(test_input_1_called_times == 1);
  assert(test_input_1_output == 4);
  temp_connect = sig.connect(&Test_SimpleAssign_1);
  sig(5);
  assert(test_input_1_output == 5);
  assert(test_input_1_called_times == 2);
}

void SignalConnectSignalTest() {
  Reset();

  signals::signal<int> sig_1;
  sig_1.connect(&Test_SimpleAssign_1);
  sig_1(5);
  assert(test_input_1_called_times == 0);
  signals::signal<int> sig_2;
  signals::connection conn_sig_slot_1 = sig_1.connect(&Test_SimpleAssign_1);
  signals::connection conn_sig_1_sig_2 = sig_1.connect(sig_2);
  sig_2.connect(&Test_SimpleAssign_1);
  sig_1(5);
  assert(test_input_1_called_times == 1);
  assert(test_input_1_output == 5);
  signals::connection conn_sig_2_slot_1 = sig_2.connect(&Test_SimpleAssign_1);
  sig_1(6);
  assert(test_input_1_called_times == 3);
  assert(test_input_1_output == 6);
  conn_sig_1_sig_2.disconnect();
  sig_1(7);
  assert(test_input_1_called_times == 4);
  assert(test_input_1_output == 7);
  sig_2(8);
  assert(test_input_1_called_times == 5);
  assert(test_input_1_output == 8);
}

void TestSignal(const signals::signal<void(int)>& signal) {
  
}

void SignalPassedByReferenceTest() {

}

void TestMultipleSlots() {
  Reset();
  signals::signal<int> sig_1;
  signals::signal<int> sig_2;
  sig_1.connect(&Test_SimpleAssign_1);
  sig_1.connect(&Test_SimpleAssign_2);

  sig_1(123);
  assert(test_input_1_called_times == 0);
  assert(test_input_2_called_times == 0);
  signals::connection conn_sig_1_slot_1 = sig_1.connect(&Test_SimpleAssign_1);
  signals::connection conn_sig_1_slot_2 = sig_1.connect(&Test_SimpleAssign_2);
  sig_1(5);
  assert(test_input_1_called_times == 1);
  assert(test_input_1_output == 5);
  assert(test_input_2_called_times == 1);
  assert(test_input_2_output == 5);
  sig_1(9999);
  assert(test_input_1_called_times == 2);
  assert(test_input_1_output == 9999);
  assert(test_input_2_called_times == 2);
  assert(test_input_2_output == 9999);
}

class TestSignalClassMember {
public:
  TestSignalClassMember(){
    
  }
  signals::signal<int, std::string&> sig_1;
  signals::signal<int, std::string&> sig_2;
};

void TestSignalAsClassMember() {
  Reset();
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
  signal_class_object_1->sig_1(100, output);
  assert(test_multiple_arguments_called_times == 1);
  assert(output == "100");
  delete signal_class_object_1;
  delete signal_class_object_3;
  delete signal_class_object_2;
}

int main(int argc, char** argv) {
  printf("Running main() from %s\n", __FILE__);
  SimpleTest();
  ConnectDisconnectTest();
  SignalConnectSignalTest();
  SignalPassedByReferenceTest();
  TestMultipleSlots();
  TestSignalAsClassMember();

  // std::tr1::shared_ptr<signals::detail::signal_detail<int>> test(new signals::detail::signal_detail<int>());

#ifdef _CRTDBG_MAP_ALLOC
  _CrtDumpMemoryLeaks();
#endif
  getchar();
}
