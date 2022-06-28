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

void BenchMarkSignalConnect(benchmark::State& state) {
  for (auto _ : state) {
    signals::signal<void, int&> simple_signal;
    simple_signal.connect(SimpleSlot);
  }
}

BENCHMARK(BenchMarkSignalConnect);

void BenchMarkBoostConnect(benchmark::State& state) {
  for (auto _ : state) {
    boost::signals2::signal<void(int&)> simple_signal;
    simple_signal.connect(SimpleSlot);
  }
}

BENCHMARK(BenchMarkBoostConnect);

void BenchMarkSignalSimple(benchmark::State& state) {
  int i = 0;
  for (auto _ : state) {
    signals::signal<void, int&> simple_signal;
    signals::connection conn = simple_signal.connect(SimpleSlot);
    simple_signal(i);
  }
  benchmark::DoNotOptimize(i);
}

BENCHMARK(BenchMarkSignalSimple);

void BenchMarkBoostSimple(benchmark::State& state) {
  int i = 0;
  for (auto _ : state) {
    boost::signals2::signal<void(int&)> simple_signal;
    boost::signals2::connection conn = simple_signal.connect(SimpleSlot);
    simple_signal(i);
  }
  benchmark::DoNotOptimize(i);
}

BENCHMARK(BenchMarkBoostSimple);

class TestClass {
public:
  void Test(int& i);
};

void TestClass::Test(int& i) {
  ++i;
}

void BenchMarkSignalClassMemberFunction(benchmark::State& state) {
  int i = 0;
  for (auto _ : state) {
    TestClass obj;
    signals::signal<void, int> simple_signal;
    signals::connection conn = simple_signal.connect(&obj, &TestClass::Test);
    simple_signal(i);
  }
  benchmark::DoNotOptimize(i);
}

BENCHMARK(BenchMarkSignalClassMemberFunction);

void BenchMarkBoostClassMemberFunction(benchmark::State& state) {
  for (auto _ : state) {
    TestClass obj;
    boost::signals2::signal<void(int&)> simple_signal;
    boost::signals2::connection conn = simple_signal.connect(boost::bind(&TestClass::Test, &obj, boost::placeholders::_1));
    int i = 2;
    simple_signal(i);
  }
}

BENCHMARK(BenchMarkBoostClassMemberFunction);

BENCHMARK_MAIN();
