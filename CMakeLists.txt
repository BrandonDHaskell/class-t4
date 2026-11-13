cmake_minimum_required(VERSION 4.0)

include(cmake/modules/version.cmake)

project(class-t4
  VERSION ${CLASS_T4_VERSION}
  DESCRIPTION "Household calendar aggregator backend"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Headers only, no C++ modules; scanning also adds GCC-only flags that break
# clang-tidy's use of the compile database.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/modules/warnings.cmake)
include(cmake/modules/sanitizers.cmake)
include(cmake/modules/reproducibility.cmake)

# Applies the project-wide settings every first-party target gets.
function(class_t4_configure_target target)
  class_t4_set_warnings(${target})
  class_t4_set_sanitizers(${target})
  class_t4_set_reproducible(${target})
endfunction()

option(CLASS_T4_BUILD_TESTS "Build unit and integration tests" ON)
if(CLASS_T4_BUILD_TESTS)
  enable_testing()
  find_package(GTest CONFIG REQUIRED)
  include(GoogleTest)

  # One GoogleTest executable per call. Discovery runs at test time (through
  # the qemu emulator for cross builds), so every case is its own ctest test.
  function(class_t4_add_unit_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LIBRARIES")
    add_executable(${name} ${arg_SOURCES})
    target_link_libraries(${name} PRIVATE ${arg_LIBRARIES} GTest::gtest_main)
    class_t4_configure_target(${name})
    gtest_discover_tests(${name}
      DISCOVERY_MODE PRE_TEST
      DISCOVERY_TIMEOUT 60)
  endfunction()
endif()

add_subdirectory(app)
if(CLASS_T4_BUILD_TESTS)
  add_subdirectory(tests/integration)
endif()
