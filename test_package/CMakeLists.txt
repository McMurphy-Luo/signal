cmake_minimum_required(VERSION 3.15)

project(signals2_test_package LANGUAGES CXX)

find_package(signals2 CONFIG REQUIRED)

add_executable(signals2_test_package test_package.cpp)
target_link_libraries(signals2_test_package PRIVATE signals2::signals2)
target_compile_features(signals2_test_package PRIVATE cxx_std_20)
