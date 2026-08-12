cmake_minimum_required(VERSION 3.15)

project(signals2_test_package LANGUAGES CXX)

find_package(signals2 CONFIG REQUIRED)

add_executable(signals2_test_package test_package.cpp)
target_link_libraries(signals2_test_package PRIVATE signals2::signals2)

# Deliberately no target_compile_features(... cxx_std_20) here: state.h needs
# C++20, so a consumer that sets no standard of its own only builds if
# signals2::signals2 propagates one.
#
# Caveat: that check only bites when this directory is configured with plain
# CMake (find_package + CMAKE_PREFIX_PATH). Under `conan create`, the generated
# toolchain sets CMAKE_CXX_STANDARD from the profile's compiler.cppstd, which
# pins C++20 globally and masks the propagation either way.
