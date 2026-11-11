# Composition root: the only place that wires modules, adapters and platform
# services together.
add_library(class_t4_cli STATIC cli/cli.cpp)
target_include_directories(class_t4_cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(class_t4_cli PUBLIC class_t4_core)
class_t4_configure_target(class_t4_cli)

add_executable(class-t4 main.cpp)
# Only this file sees the version and commit, so a new commit relinks the
# binary without rebuilding the libraries.
target_compile_definitions(class-t4 PRIVATE
  CLASS_T4_VERSION="${CLASS_T4_VERSION}"
  CLASS_T4_COMMIT="${CLASS_T4_COMMIT}")
target_link_libraries(class-t4 PRIVATE class_t4_cli class_t4_platform_drogon)
class_t4_configure_target(class-t4)

install(TARGETS class-t4 RUNTIME DESTINATION bin)

if(CLASS_T4_BUILD_TESTS)
  add_subdirectory(tests)
  add_test(NAME class-t4.version COMMAND class-t4 --version)
  set_tests_properties(class-t4.version PROPERTIES
    PASS_REGULAR_EXPRESSION "^class-t4 ${CLASS_T4_VERSION} ")
endif()
