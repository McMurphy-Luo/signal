#define CATCH_CONFIG_RUNNER
#include "catch.hpp"
#include <cstdio>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

int main(int argc, char** argv) {
  printf("Running main() from %s\n", __FILE__);

#ifdef _MSC_VER
  int flag = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
  flag |= _CRTDBG_LEAK_CHECK_DF;
  flag |= _CRTDBG_ALLOC_MEM_DF;
  _CrtSetDbgFlag(flag);
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
  _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
  _CrtSetBreakAlloc(-1);
#endif
  return Catch::Session().run(argc, argv);
}
