# Shared kernel. Must never link Drogon, SQLite, or any HTTP or TLS library.
find_package(jsoncpp CONFIG REQUIRED)

add_library(class_t4_core STATIC
  src/config.cpp
  src/health.cpp
  src/metrics.cpp)
target_include_directories(class_t4_core PUBLIC include)
target_link_libraries(class_t4_core PRIVATE JsonCpp::JsonCpp)
class_t4_configure_target(class_t4_core)

if(CLASS_T4_BUILD_TESTS)
  add_subdirectory(tests)
endif()
