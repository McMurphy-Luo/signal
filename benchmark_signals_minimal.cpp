#include <cassert>
#include <iostream>
#include "signals_minimal.h"
#include "boost/signals2.hpp"
#include "benchmark/benchmark.h"

void SimpleSlot(int& i) {
  ++i;
  benchmark::DoNotOptimize(i);
}

void BenchMarkZero(benchmark::State& state) {
  for (auto _ : state) {
    int i = 2;
    SimpleSlot(i);
  }
}

BENCHMARK(BenchMarkZero);

void BenchMarkSimpleNewFree(benchmark::State& state) {
  for (auto _ : state) {
    int* p_test = new int{ 5 };
    delete p_test;
  }
}

BENCHMARK(BenchMarkSimpleNewFree);

void BenchMarkSharedPtr(benchmark::State& state) {
  for (auto _ : state) {
    std::make_shared<int>(5);
  }
}

BENCHMARK(BenchMarkSharedPtr);

void BenchMarkSignalConnect(benchmark::State& state) {
  signals::signal<void, int&> simple_signal;
  for (auto _ : state) {
    simple_signal.connect(SimpleSlot);
  }
}

BENCHMARK(BenchMarkSignalConnect);

void BenchMarkBoostConnect(benchmark::State& state) {
  boost::signals2::signal<void(int&)> simple_signal;
  for (auto _ : state) {
    simple_signal.connect(SimpleSlot);
  }
}

BENCHMARK(BenchMarkBoostConnect);

void BenchMarkSignalTrigger(benchmark::State& state) {
  int i = 0;
  signals::signal<void, int&> simple_signal;
  signals::connection conn = simple_signal.connect(SimpleSlot);
  for (auto _ : state) {
    simple_signal(i);
  }
  benchmark::DoNotOptimize(i);
}

BENCHMARK(BenchMarkSignalTrigger);

void BenchMarkBoostTrigger(benchmark::State& state) {
  int i = 0;
  boost::signals2::signal<void(int&)> simple_signal;
  boost::signals2::connection conn = simple_signal.connect(SimpleSlot);
  for (auto _ : state) {
    simple_signal(i);
  }
  benchmark::DoNotOptimize(i);
}

BENCHMARK(BenchMarkBoostTrigger);

class TestClass {
public:
  void Test(int i);
};

void TestClass::Test(int i) {
  ++i;
}

void BenchMarkSignalConnectClassMemberFunction(benchmark::State& state) {
  signals::signal<void, int> simple_signal;
  for (auto _ : state) {
    TestClass obj;
    simple_signal.connect(&obj, &TestClass::Test);
  }
}

BENCHMARK(BenchMarkSignalConnectClassMemberFunction);

void BenchMarkBoostConnectClassMemberFunction(benchmark::State& state) {
  boost::signals2::signal<void(int&)> simple_signal;
  for (auto _ : state) {
    TestClass obj;
    simple_signal.connect(boost::bind(&TestClass::Test, &obj, boost::placeholders::_1));
  }
}

BENCHMARK(BenchMarkBoostConnectClassMemberFunction);

BENCHMARK_MAIN();
