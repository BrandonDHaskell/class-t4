# Cross toolchain for the Raspberry Pi 5 (aarch64, Debian 13 sysroot).
# Used directly by the app build (chainloaded by vcpkg) and by the
# arm64-linux-rpi5 triplet for every vcpkg port.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CLASS_T4_CROSS_PREFIX "/opt/gcc-cross/bin/aarch64-linux-gnu-")
set(CMAKE_C_COMPILER "${CLASS_T4_CROSS_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${CLASS_T4_CROSS_PREFIX}g++")
set(CMAKE_AR "/usr/bin/aarch64-linux-gnu-ar")
set(CMAKE_RANLIB "/usr/bin/aarch64-linux-gnu-ranlib")
set(CMAKE_STRIP "/usr/bin/aarch64-linux-gnu-strip")

set(CMAKE_SYSROOT "$ENV{SYSROOT}")
if(NOT CMAKE_SYSROOT)
  set(CMAKE_SYSROOT /opt/sysroot)
endif()

# Ports build under /cache (vcpkg buildtrees) and the app under /src; mapping
# both keeps absolute build paths out of every object that is linked in.
set(CLASS_T4_TARGET_FLAGS
    "-mcpu=cortex-a76 -ffile-prefix-map=/src=. -ffile-prefix-map=/cache=.")
set(CMAKE_C_FLAGS_INIT "${CLASS_T4_TARGET_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${CLASS_T4_TARGET_FLAGS}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Lets ctest run aarch64 test binaries on the x86_64 build host.
set(CMAKE_CROSSCOMPILING_EMULATOR "/usr/bin/qemu-aarch64;-L;${CMAKE_SYSROOT}")
