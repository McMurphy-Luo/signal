cmake_minimum_required(VERSION 3.15)

project(signals2 VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

add_library(signals2 INTERFACE)
add_library(signals2::signals2 ALIAS signals2)

target_include_directories(
  signals2 INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_compile_features(signals2 INTERFACE cxx_std_14)

install(
  TARGETS signals2
  EXPORT signals2_export
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
  DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  FILES_MATCHING PATTERN "*.h"
)

install(
  EXPORT signals2_export
  FILE signals2Targets.cmake
  NAMESPACE signals2::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/signals2"
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/signals2Config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/signals2Config.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/signals2"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/signals2ConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/signals2Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/signals2ConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/signals2"
)
