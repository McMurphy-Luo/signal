find_package(Boost CONFIG REQUIRED)

if(SIGNALS2_BUILD_TESTS)
  add_executable(
    signals2_tests
    main.cpp
    signals_test.cpp
    state_test.cpp
    catch_amalgamated.cpp
    catch_amalgamated.hpp
  )
  target_include_directories(signals2_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(signals2_tests PRIVATE signals2::signals2 Boost::boost)
  target_compile_definitions(signals2_tests PRIVATE SIGNALS_ENABLE_TEST_ACCESS CATCH_AMALGAMATED_CUSTOM_MAIN)
  set_target_properties(signals2_tests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
  )

  if(MSVC)
    target_compile_definitions(signals2_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()

  add_test(NAME signals2_tests COMMAND signals2_tests)
endif()

if(SIGNALS2_BUILD_BENCHMARKS)
  find_package(benchmark CONFIG REQUIRED)
  add_executable(signals_benchmark signals_benchmark.cpp)
  target_link_libraries(
    signals_benchmark PRIVATE
    signals2::signals2
    Boost::boost
    benchmark::benchmark
  )
  set_target_properties(signals_benchmark PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_definitions(signals_benchmark PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
endif()
