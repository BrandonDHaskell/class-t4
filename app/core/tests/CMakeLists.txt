find_package(jsoncpp CONFIG REQUIRED)

class_t4_add_unit_test(class_t4_core_tests
  SOURCES config_test.cpp health_test.cpp metrics_test.cpp
  LIBRARIES class_t4_core JsonCpp::JsonCpp)
target_compile_definitions(class_t4_core_tests PRIVATE
  CLASS_T4_EXAMPLE_CONFIG="${PROJECT_SOURCE_DIR}/config/class-t4.example.json")
