# Toolchain image

Every class-t4 build runs in this image, referenced only by the digest in `DIGEST`.

## Contents

| Component | Version | Location |
| --- | --- | --- |
| Base | `debian:trixie`, pinned by digest; apt from a fixed `snapshot.debian.org` timestamp | |
| GCC (x86_64) | 16.2.0, built in the image | `/opt/gcc`, first on `PATH` (`gcc`, `cc`, `g++`, `c++`) |
| GCC (aarch64 cross) | 16.2.0, built against the sysroot | `/opt/gcc-cross`, `aarch64-linux-gnu-*` |
| Sysroot | Debian 13 arm64, glibc 2.41 | `/opt/sysroot` (`$SYSROOT`) |
| clang-format, clang-tidy | 23.1.1 (official LLVM release) | `/opt/llvm` |
| CMake | 4.4.3 | `/opt/cmake` |
| Ninja | 1.13.1 | `/opt/ninja` |
| vcpkg | release `2026.10.16` (commit `9eec002d`) | `/opt/vcpkg` (`$VCPKG_ROOT`) |
| qemu-user | Debian 13 | `qemu-aarch64`, `QEMU_LD_PREFIX=/opt/sysroot` |

`/opt/<tool>` are unversioned symlinks to `/opt/<tool>-<version>`. vcpkg's downloads and binary cache default to `/cache`; the image runs as any uid. The image deliberately sets no `CC`/`CXX`, so cross builds of make-based ports (OpenSSL) never pick up the native compiler.

`smoke-test.sh` runs at build time and can be rerun in the image. It checks the versions, builds C++23 for both targets, runs the aarch64 binary under qemu, and enforces the release rules (glibc-only `NEEDED`, highest `GLIBC_` at most 2.41).

## Updating

1. Change pins in `Dockerfile` (each download has a SHA-256; the GCC tarball's GPG signature should be checked when bumping it). If the vcpkg commit changes, update the baseline in `vcpkg-configuration.json` to match.
2. Push. `.github/workflows/toolchain.yml` builds and pushes `ghcr.io/brandondhaskell/class-t4-toolchain:sha-<commit>` and prints the digest in the job summary.
3. Copy the `ghcr.io/...@sha256:...` reference into `DIGEST` and commit it.

Local build: `docker buildx build --load -t class-t4-toolchain:local docker/toolchain`.
