# signals2

`signals2` is a header-only C++20 signals/slots library. CMake is the project
entry point and Conan 2 supplies the dependency files and CMake toolchain used
for development builds.

## Build and test with Conan

Install the test dependencies and generate the Conan CMake presets:

```sh
conan install . --output-folder=build/conan --build=missing \
  -s build_type=Release -s:h compiler.cppstd=20 -o "&:build_tests=True"
cmake --preset conan-default
cmake --build --preset conan-release
ctest --preset conan-release --output-on-failure
```

Add `-o "&:build_benchmarks=True"` to the `conan install` command to build
`signals_benchmark`. Run `conan install` once per build configuration when
using a multi-configuration generator.

## Create and consume the Conan package

Create the header-only package and run its consumer smoke test:

```sh
conan create . --build=missing -s:h compiler.cppstd=20
```

Consumers use the generated CMake dependency files in the usual way:

```cmake
find_package(signals2 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE signals2::signals2)
```

The public header is included as:

```cpp
#include <signals/signals.h>
```

## CMake without Conan

When Boost and, optionally, Google Benchmark are already available as CMake
config packages, a regular build also works:

```sh
cmake -S . -B build/local -DSIGNALS2_BUILD_TESTS=ON
cmake --build build/local --config Release
ctest --test-dir build/local -C Release --output-on-failure
```

For installation, use `cmake --install`; the installed package exports the
same `signals2::signals2` target.
