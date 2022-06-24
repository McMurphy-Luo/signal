#include "benchmark/benchmark.h"
#include "signals_minimal.h"
#include <cassert>
#include <iostream>

using signals::signal;
using signals::connection;

void SimpleSlot(int& i) {
  ++i;
}

void BenchMarkSimple(benchmark::State& state) {
  int i = 0;
  for (auto _ : state) {
    signal<void, int&> simple_signal;
    connection conn = simple_signal.connect(SimpleSlot);
    simple_signal(i);
  }
  benchmark::DoNotOptimize(i);
}

BENCHMARK(BenchMarkSimple);

class TestClass {
public:
  void Test(int& i);
};

void TestClass::Test(int& i) {
  ++i;
}

void BenchMarkClassMemberFunction(benchmark::State& state) {
  int i = 0;
  for (auto _ : state) {
    TestClass obj;
    signal<void, int> simple_signal;
    connection conn = simple_signal.connect(&obj, &TestClass::Test);
    simple_signal(i);
  }
  benchmark::DoNotOptimize(i);
}

BENCHMARK(BenchMarkClassMemberFunction);

BENCHMARK_MAIN();
