find_package(Drogon CONFIG REQUIRED)

add_executable(class-t4 main.cpp)
target_compile_definitions(class-t4 PRIVATE
  CLASS_T4_VERSION="${CLASS_T4_VERSION}"
  CLASS_T4_COMMIT="${CLASS_T4_COMMIT}")
target_link_libraries(class-t4 PRIVATE Drogon::Drogon)
class_t4_configure_target(class-t4)

install(TARGETS class-t4 RUNTIME DESTINATION bin)

if(CLASS_T4_BUILD_TESTS)
  add_test(NAME class-t4.version COMMAND class-t4 --version)
  set_tests_properties(class-t4.version PROPERTIES
    PASS_REGULAR_EXPRESSION "^class-t4 ${CLASS_T4_VERSION} ")
endif()
