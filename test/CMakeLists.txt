cmake_minimum_required(VERSION 3.15)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

project(signals2_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SIGNALS2_BUILD_BENCHMARK "Build Google Benchmark executable" OFF)

find_package(Boost REQUIRED)

get_filename_component(SIGNAL_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

add_executable(signals_tests main.cpp signals_test.cpp)
target_include_directories(
  signals_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${SIGNAL_ROOT}/include/signals"
)
target_link_libraries(signals_tests PRIVATE Boost::boost)

target_compile_definitions(signals_tests PRIVATE SIGNALS_ENABLE_TEST_ACCESS)

if(MSVC)
  target_compile_definitions(signals_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

include(CTest)
enable_testing()
add_test(NAME signals_tests COMMAND signals_tests)

if(SIGNALS2_BUILD_BENCHMARK)
  find_package(benchmark REQUIRED)
  add_executable(signals_benchmark signals_benchmark.cpp)
  target_include_directories(
    signals_benchmark PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SIGNAL_ROOT}/include/signals"
  )
  target_link_libraries(signals_benchmark PRIVATE Boost::boost benchmark::benchmark)
  if(MSVC)
    target_compile_definitions(signals_benchmark PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
endif()
