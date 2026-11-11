# Raspberry Pi 5 (Cortex-A76), Debian 13, glibc 2.41. Every dependency is a
# static library so the release binary's NEEDED entries are glibc only.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_BUILD_TYPE release)

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../cmake/toolchains/aarch64-rpi5.cmake")

# Compiler flags (-mcpu=cortex-a76 and the path prefix maps) live in the
# chainloaded toolchain file: vcpkg ignores VCPKG_C_FLAGS/VCPKG_CXX_FLAGS when a
# triplet chainloads its own toolchain.

# Not passed through to the port build environment by default.
set(VCPKG_ENV_PASSTHROUGH SOURCE_DATE_EPOCH)
