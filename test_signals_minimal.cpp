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

using signals::signal;
using signals::connection;

TEST_CASE("Test no arguments signal") {
  signal<void> signal_without_arguments;
  int slot_called_times = 0;
  connection test_conn = signal_without_arguments.connect([&slot_called_times = slot_called_times]() {
    ++slot_called_times;
  });
  signal_without_arguments();
  CHECK(slot_called_times == 1);
}
