# Drogon adapter for process-level HTTP: listeners, TLS, /health, /metrics.
find_package(Drogon CONFIG REQUIRED)

add_library(class_t4_platform_drogon STATIC src/server.cpp)
target_include_directories(class_t4_platform_drogon PUBLIC include)
target_link_libraries(class_t4_platform_drogon
  PUBLIC class_t4_core
  PRIVATE Drogon::Drogon)
class_t4_configure_target(class_t4_platform_drogon)
