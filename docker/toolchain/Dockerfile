# syntax=docker/dockerfile:1
#
# class-t4 toolchain image. Every build (CI and lovelace) runs in this image,
# referenced only by the digest recorded in docker/toolchain/DIGEST.
#
# Pinning: the base image is pinned by digest, apt reads from a fixed
# snapshot.debian.org timestamp, and every downloaded artifact is checked
# against a SHA-256 below. Changing any pin means a new image and a new DIGEST.

ARG DEBIAN_IMAGE=debian:trixie@sha256:9c6e381421d0f48d1b8133e573f33c185ee16cbf505eda9c4a8a4791b25ea8a6

# ---------------------------------------------------------------------------
# base: Debian with apt pointed at a fixed snapshot
# ---------------------------------------------------------------------------
FROM ${DEBIAN_IMAGE} AS base

ARG SNAPSHOT=20261111T000000Z
ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8
SHELL ["/bin/bash", "-euo", "pipefail", "-c"]

# Plain http is fine: apt verifies the Release signatures. Valid-Until is
# ignored because a pinned snapshot is expected to age.
RUN rm -f /etc/apt/sources.list.d/debian.sources \
 && printf '%s\n' \
      'Types: deb' \
      "URIs: http://snapshot.debian.org/archive/debian/${SNAPSHOT}" \
      'Suites: trixie trixie-updates' \
      'Components: main' \
      'Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg' \
      '' \
      'Types: deb' \
      "URIs: http://snapshot.debian.org/archive/debian-security/${SNAPSHOT}" \
      'Suites: trixie-security' \
      'Components: main' \
      'Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg' \
      > /etc/apt/sources.list.d/snapshot.sources \
 && printf '%s\n' \
      'Acquire::Check-Valid-Until "false";' \
      'Acquire::Retries "5";' \
      'APT::Install-Recommends "false";' \
      > /etc/apt/apt.conf.d/99snapshot \
 && apt-get update

# ---------------------------------------------------------------------------
# sysroot: Debian 13 arm64 glibc 2.41 and kernel headers
# ---------------------------------------------------------------------------
FROM base AS sysroot

ARG GLIBC_VERSION=2.41

RUN dpkg --add-architecture arm64 \
 && apt-get update \
 && mkdir -p /tmp/debs /opt/sysroot \
 && cd /tmp/debs \
 && apt-get download libc6:arm64 libc6-dev:arm64 linux-libc-dev \
 && test -n "$(compgen -G 'linux-libc-dev_*_all.deb')" \
 && for deb in *.deb; do dpkg-deb -x "$deb" /opt/sysroot; done \
 && rm -rf /tmp/debs

# The tree has no base-files, so recreate the usrmerge layout Debian 13
# expects (lib -> usr/lib), then make absolute symlinks relative so the
# sysroot works from any mount point.
RUN cd /opt/sysroot \
 && for d in bin sbin lib; do \
      if [ -d "$d" ] && [ ! -L "$d" ]; then \
        mkdir -p "usr/$d" && cp -a "$d/." "usr/$d/" && rm -rf "$d"; \
      fi; \
      [ -e "$d" ] || ln -s "usr/$d" "$d"; \
    done \
 && find . -type l -lname '/*' -print0 | while IFS= read -r -d '' link; do \
      target="$(readlink "$link")"; \
      ln -sfn "$(realpath -m --relative-to="$(dirname "$link")" "/opt/sysroot${target}")" "$link"; \
    done \
 && test -e lib/ld-linux-aarch64.so.1 \
 && test -e usr/lib/aarch64-linux-gnu/libc.so \
 && grep -aq "GNU C Library (Debian GLIBC ${GLIBC_VERSION}-" usr/lib/aarch64-linux-gnu/libc.so.6 \
 && echo "sysroot glibc ${GLIBC_VERSION} ok"

# ---------------------------------------------------------------------------
# gcc-build-base: build deps and verified GCC source
# ---------------------------------------------------------------------------
FROM base AS gcc-build-base

RUN apt-get install -y \
      build-essential binutils-aarch64-linux-gnu ca-certificates curl \
      libgmp-dev libmpfr-dev libmpc-dev libisl-dev libzstd-dev zlib1g-dev \
      flex bison texinfo xz-utils file

ARG GCC_VERSION=16.2.0
ARG GCC_SHA256=e6738e29597f733270731aa90600f37ffdc045079dfc27ec7e8192cc81085c3e

RUN curl -fsSL -o /tmp/gcc.tar.xz \
      "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz" \
 && echo "${GCC_SHA256}  /tmp/gcc.tar.xz" | sha256sum -c - \
 && mkdir -p /usr/src/gcc \
 && tar -xJf /tmp/gcc.tar.xz -C /usr/src/gcc --strip-components=1 \
 && rm /tmp/gcc.tar.xz

# ---------------------------------------------------------------------------
# gcc-native: GCC for x86_64 (host-debug preset, tooling)
# ---------------------------------------------------------------------------
FROM gcc-build-base AS gcc-native

ARG GCC_VERSION=16.2.0

RUN mkdir /tmp/build && cd /tmp/build \
 && /usr/src/gcc/configure \
      --prefix=/opt/gcc-${GCC_VERSION%.*} \
      --build=x86_64-linux-gnu --host=x86_64-linux-gnu --target=x86_64-linux-gnu \
      --enable-languages=c,c++ \
      --disable-multilib --disable-bootstrap --disable-nls \
      --enable-checking=release --with-system-zlib \
 && make -j"$(nproc)" \
 && make install-strip \
 && rm -rf /tmp/build

# ---------------------------------------------------------------------------
# gcc-cross: GCC for aarch64-linux-gnu against the glibc 2.41 sysroot
# ---------------------------------------------------------------------------
FROM gcc-build-base AS gcc-cross

ARG GCC_VERSION=16.2.0
COPY --from=sysroot /opt/sysroot /opt/sysroot

# libstdc++ and libgcc are built against the 2.41 sysroot, so statically
# linking them never pulls in a newer GLIBC_ symbol version.
RUN mkdir /tmp/build && cd /tmp/build \
 && /usr/src/gcc/configure \
      --prefix=/opt/gcc-cross-${GCC_VERSION%.*} \
      --build=x86_64-linux-gnu --host=x86_64-linux-gnu --target=aarch64-linux-gnu \
      --with-sysroot=/opt/sysroot \
      --with-build-sysroot=/opt/sysroot \
      --with-as=/usr/bin/aarch64-linux-gnu-as \
      --with-ld=/usr/bin/aarch64-linux-gnu-ld \
      --with-arch=armv8.2-a \
      --enable-languages=c,c++ \
      --disable-multilib --disable-bootstrap --disable-nls \
      --disable-libsanitizer --enable-checking=release \
      --enable-threads=posix --enable-__cxa_atexit --enable-clocale=gnu \
 && make -j"$(nproc)" \
 && make install-strip \
 && rm -rf /tmp/build

# ---------------------------------------------------------------------------
# tools: LLVM clang-format/clang-tidy, CMake, Ninja, vcpkg
# ---------------------------------------------------------------------------
FROM base AS tools

RUN apt-get install -y ca-certificates curl git unzip xz-utils zip tar

ARG LLVM_VERSION=23.1.1
ARG LLVM_SHA256=832aeb58d105de1cabc7b982dd2c65de0610f7377df48ae8fc2dd8e97420a15c
ARG CMAKE_VERSION=4.4.3
ARG CMAKE_SHA256=d6c83076c575bc00b823522ac974bda66d0af05d6ddc30e739c12385cf32c6cc
ARG NINJA_VERSION=1.13.1
ARG NINJA_SHA256=0830252db77884957a1a4b87b05a1e2d9b5f658b8367f82999a941884cbe0238

# Only clang-format, clang-tidy and the builtin headers clang-tidy needs.
RUN curl -fsSL -o /tmp/llvm.tar.xz \
      "https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/LLVM-${LLVM_VERSION}-Linux-X64.tar.xz" \
 && echo "${LLVM_SHA256}  /tmp/llvm.tar.xz" | sha256sum -c - \
 && mkdir -p /opt/llvm-${LLVM_VERSION} \
 && tar -xJf /tmp/llvm.tar.xz -C /opt/llvm-${LLVM_VERSION} --strip-components=1 \
      "LLVM-${LLVM_VERSION}-Linux-X64/bin/clang-format" \
      "LLVM-${LLVM_VERSION}-Linux-X64/bin/clang-tidy" \
      "LLVM-${LLVM_VERSION}-Linux-X64/lib/clang" \
 && rm /tmp/llvm.tar.xz

RUN curl -fsSL -o /tmp/cmake.tar.gz \
      "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" \
 && echo "${CMAKE_SHA256}  /tmp/cmake.tar.gz" | sha256sum -c - \
 && mkdir -p /opt/cmake-${CMAKE_VERSION} \
 && tar -xzf /tmp/cmake.tar.gz -C /opt/cmake-${CMAKE_VERSION} --strip-components=1 \
      --exclude='*/doc' --exclude='*/man' \
 && rm /tmp/cmake.tar.gz

RUN curl -fsSL -o /tmp/ninja.zip \
      "https://github.com/ninja-build/ninja/releases/download/v${NINJA_VERSION}/ninja-linux.zip" \
 && echo "${NINJA_SHA256}  /tmp/ninja.zip" | sha256sum -c - \
 && mkdir -p /opt/ninja-${NINJA_VERSION}/bin \
 && unzip -q /tmp/ninja.zip -d /opt/ninja-${NINJA_VERSION}/bin \
 && rm /tmp/ninja.zip

# vcpkg release 2026.10.16. vcpkg-configuration.json uses this commit as its
# baseline. bootstrap-vcpkg.sh fetches the vcpkg tool pinned (with SHA-512)
# by scripts/vcpkg-tool-metadata.txt at this commit.
ARG VCPKG_COMMIT=9eec002df1cd109f8f104cd966a763daf9751489
RUN git init -q /opt/vcpkg \
 && cd /opt/vcpkg \
 && git remote add origin https://github.com/microsoft/vcpkg.git \
 && git fetch -q --depth=1 origin "${VCPKG_COMMIT}" \
 && git -c advice.detachedHead=false checkout -q FETCH_HEAD \
 && test "$(git rev-parse HEAD)" = "${VCPKG_COMMIT}" \
 && ./bootstrap-vcpkg.sh -disableMetrics \
 && rm -rf .git/hooks

# ---------------------------------------------------------------------------
# final
# ---------------------------------------------------------------------------
FROM base AS final

LABEL org.opencontainers.image.source="https://github.com/BrandonDHaskell/class-t4" \
      org.opencontainers.image.description="class-t4 build toolchain: GCC 16.2 (x86_64, aarch64 cross), Debian 13 arm64 sysroot, Clang 23.1.1 tools, CMake, Ninja, vcpkg" \
      org.opencontainers.image.licenses="Apache-2.0"

# Runtime packages: binutils for both targets, GCC's own runtime deps,
# libstdc++/zlib for the LLVM binaries, what vcpkg ports commonly need to
# build, qemu-user for aarch64 tests, and openssl for throwaway test certs.
RUN apt-get install -y \
      binutils binutils-aarch64-linux-gnu libc6-dev \
      libgmp10 libmpfr6 libmpc3 libisl23 libzstd1 zlib1g libstdc++6 \
      ca-certificates curl git zip unzip tar xz-utils gzip bzip2 file \
      make pkg-config autoconf automake autoconf-archive libtool \
      bison flex perl python3 python3-yaml \
      linux-libc-dev \
      qemu-user openssl jq \
 && apt-get clean \
 && rm -rf /var/lib/apt/lists/*

COPY --from=sysroot /opt/sysroot /opt/sysroot
COPY --from=gcc-native /opt/gcc-16.2 /opt/gcc-16.2
COPY --from=gcc-cross /opt/gcc-cross-16.2 /opt/gcc-cross-16.2
COPY --from=tools /opt/llvm-23.1.1 /opt/llvm-23.1.1
COPY --from=tools /opt/cmake-4.4.3 /opt/cmake-4.4.3
COPY --from=tools /opt/ninja-1.13.1 /opt/ninja-1.13.1
COPY --from=tools /opt/vcpkg /opt/vcpkg

# Unversioned symlinks, so scripts never name a version. vcpkg's root is
# read-only to the build user; its downloads and binary cache live under
# /cache, which build-in-container.sh can mount. The native GCC's libstdc++
# is registered with ldconfig so host-debug binaries and tests run as built.
RUN ln -s gcc-16.2 /opt/gcc \
 && ln -s gcc-cross-16.2 /opt/gcc-cross \
 && ln -s llvm-23.1.1 /opt/llvm \
 && ln -s cmake-4.4.3 /opt/cmake \
 && ln -s ninja-1.13.1 /opt/ninja \
 && mkdir -p /opt/bin \
 && for t in clang-format clang-tidy; do ln -s ../llvm/bin/$t /opt/bin/$t; done \
 && for t in cmake ctest cpack; do ln -s ../cmake/bin/$t /opt/bin/$t; done \
 && ln -s ../ninja/bin/ninja /opt/bin/ninja \
 && ln -s ../vcpkg/vcpkg /opt/bin/vcpkg \
 && chmod -R a+rX /opt \
 && mkdir -m 1777 /cache \
 && echo /opt/gcc/lib64 > /etc/ld.so.conf.d/00-opt-gcc.conf \
 && ldconfig

ENV PATH=/opt/gcc/bin:/opt/gcc-cross/bin:/opt/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    CC=/opt/gcc/bin/gcc \
    CXX=/opt/gcc/bin/g++ \
    SYSROOT=/opt/sysroot \
    QEMU_LD_PREFIX=/opt/sysroot \
    VCPKG_ROOT=/opt/vcpkg \
    VCPKG_DISABLE_METRICS=1 \
    VCPKG_DOWNLOADS=/cache/vcpkg/downloads \
    VCPKG_DEFAULT_BINARY_CACHE=/cache/vcpkg/archives \
    HOME=/tmp

# Smoke test: both compilers build C++23, the aarch64 binary runs under qemu,
# links only glibc dynamically, and needs no GLIBC_ version above 2.41.
COPY smoke-test.sh /opt/toolchain/smoke-test.sh
RUN /opt/toolchain/smoke-test.sh

WORKDIR /src
