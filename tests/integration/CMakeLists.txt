# Whole-binary test: starts class-t4, probes both listeners with curl, then
# sends SIGTERM. Cross builds run the binary through the qemu emulator.
set(emulator "")
if(CMAKE_CROSSCOMPILING_EMULATOR)
  set(emulator ${CMAKE_CROSSCOMPILING_EMULATOR})
endif()

add_test(NAME integration.serve
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/serve_test.sh $<TARGET_FILE:class-t4> ${emulator})
set_tests_properties(integration.serve PROPERTIES
  RESOURCE_LOCK class_t4_test_ports
  TIMEOUT 180)
if(CMAKE_CROSSCOMPILING)
  # qemu-user starts the binary noticeably slower.
  set_tests_properties(integration.serve PROPERTIES
    ENVIRONMENT "CLASS_T4_STARTUP_TIMEOUT=30")
endif()
