#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

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
