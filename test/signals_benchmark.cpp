/**
 * NOTE on the connect benchmarks below.
 *
 * signals2::connection is an *owning* handle -- destroying it disconnects the
 * slot. boost::signals2::connection is a non-owning handle -- destroying it
 * leaves the slot connected. So discarding the return value does not mean the
 * same thing on the two sides, and a benchmark that discards it on both is not
 * measuring the same work: our signal would stay empty while boost's slot list
 * grows without bound.
 *
 * Every connect benchmark here therefore measures connect + immediate
 * disconnect, with boost using scoped_connection to match. That keeps the two
 * symmetric and the slot list bounded. If you would rather measure connect
 * alone as the slot list grows, both sides must retain their connections --
 * and then both need a periodic, timing-paused clear to bound memory.
 */

#include <cassert>
#include <iostream>
#include <memory>
#include <signals/signals.h>
#include "boost/signals2.hpp"
#include "benchmark/benchmark.h"

void SimpleSlot(int& i) {
  ++i;
  benchmark::DoNotOptimize(i);
}

void BenchMarkZero(benchmark::State& state) {
  int i = 2;
  for (auto _ : state) {
    SimpleSlot(i);
  }
  benchmark::DoNotOptimize(i);
}

BENCHMARK(BenchMarkZero);

void BenchMarkSimpleNewFree(benchmark::State& state) {
  for (auto _ : state) {
    int* p_test = new int{ 5 };
    benchmark::DoNotOptimize(p_test);
    delete p_test;
  }
}

BENCHMARK(BenchMarkSimpleNewFree);

void BenchMarkSharedPtr(benchmark::State& state) {
  for (auto _ : state) {
    // Bound and clobbered: a discarded make_shared has no observable effect
    // and the allocation is free to be elided.
    std::shared_ptr<int> p = std::make_shared<int>(5);
    benchmark::DoNotOptimize(p);
  }
}

BENCHMARK(BenchMarkSharedPtr);

void BenchMarkSignalConnectDisconnect(benchmark::State& state) {
  signals2::signal2<void, int&> simple_signal;
  for (auto _ : state) {
    signals2::connection conn = simple_signal.connect(SimpleSlot);
    benchmark::DoNotOptimize(conn);
  }  // owning handle -- disconnects here
}

BENCHMARK(BenchMarkSignalConnectDisconnect);

void BenchMarkBoostConnectDisconnect(benchmark::State& state) {
  boost::signals2::signal<void(int&)> simple_signal;
  for (auto _ : state) {
    boost::signals2::scoped_connection conn = simple_signal.connect(SimpleSlot);
    benchmark::DoNotOptimize(conn);
  }  // scoped_connection -- disconnects here, matching signals2
}

BENCHMARK(BenchMarkBoostConnectDisconnect);

void BenchMarkSimpleFunctionObject(benchmark::State& state) {
  std::function<void(int&)> f(SimpleSlot);
  int i = 0;
  for (auto _ : state) {
    f(i);
  }
  benchmark::DoNotOptimize(i);
}

BENCHMARK(BenchMarkSimpleFunctionObject);

void BenchMarkSignalTrigger(benchmark::State& state) {
  int i = 0;
  signals2::signal2<void, int&> simple_signal;
  signals2::connection conn = simple_signal.connect(SimpleSlot);
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

void BenchMarkSignalTriggerMultipleSlots(benchmark::State& state) {
  int i = 0;
  signals2::signal2<void, int&> simple_signal;
  signals2::connection conn_1 = simple_signal.connect(SimpleSlot);
  signals2::connection conn_2 = simple_signal.connect(SimpleSlot);
  signals2::connection conn_3 = simple_signal.connect(SimpleSlot);
  for (auto _ : state) {
    simple_signal(i);
  }
  benchmark::DoNotOptimize(i);
}

BENCHMARK(BenchMarkSignalTriggerMultipleSlots);

void BenchMarkBoostTriggerMultipleSlots(benchmark::State& state) {
  int i = 0;
  boost::signals2::signal<void(int&)> simple_signal;
  boost::signals2::connection conn_1 = simple_signal.connect(SimpleSlot);
  boost::signals2::connection conn_2 = simple_signal.connect(SimpleSlot);
  boost::signals2::connection conn_3 = simple_signal.connect(SimpleSlot);
  for (auto _ : state) {
    simple_signal(i);
  }
  benchmark::DoNotOptimize(i);
}

BENCHMARK(BenchMarkBoostTriggerMultipleSlots);

class TestClass {
public:
  void Test(int i);
};

void TestClass::Test(int i) {
  ++i;
}

void BenchMarkSignalConnectDisconnectClassMemberFunction(benchmark::State& state) {
  signals2::signal2<void, int> simple_signal;
  TestClass obj;
  for (auto _ : state) {
    signals2::connection conn = simple_signal.connect(&obj, &TestClass::Test);
    benchmark::DoNotOptimize(conn);
  }
}

BENCHMARK(BenchMarkSignalConnectDisconnectClassMemberFunction);

// void(int), not void(int&) -- must match the signals2 signal above.
void BenchMarkBoostConnectDisconnectClassMemberFunction(benchmark::State& state) {
  boost::signals2::signal<void(int)> simple_signal;
  TestClass obj;
  for (auto _ : state) {
    boost::signals2::scoped_connection conn = simple_signal.connect(
        boost::bind(&TestClass::Test, &obj, boost::placeholders::_1));
    benchmark::DoNotOptimize(conn);
  }
}

BENCHMARK(BenchMarkBoostConnectDisconnectClassMemberFunction);

BENCHMARK_MAIN();
