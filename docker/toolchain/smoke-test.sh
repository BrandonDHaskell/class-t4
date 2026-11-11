#!/usr/bin/env bash
# Toolchain smoke test, run at image build time and safe to rerun inside the
# image. Checks versions, builds C++23 for both targets, runs the aarch64
# binary under qemu, and enforces the release linking rules: only glibc in
# NEEDED and no GLIBC_ symbol version above the epiphany ceiling.
set -euo pipefail

readonly GLIBC_CEILING=2.41
readonly SYSROOT=${SYSROOT:-/opt/sysroot}

expect_version() {
  local name=$1 want=$2 got=$3
  if [[ $got != *"$want"* ]]; then
    echo "FAIL: $name: want $want, got: $got" >&2
    exit 1
  fi
  echo "ok: $name $want"
}

expect_version gcc 16.2.0 "$(gcc --version | head -1)"
expect_version cc 16.2.0 "$(cc --version | head -1)"
expect_version c++ 16.2.0 "$(c++ --version | head -1)"
[[ -z ${CC:-}${CXX:-} ]] || { echo "FAIL: CC/CXX must not be set in the image" >&2; exit 1; }
expect_version aarch64-linux-gnu-g++ 16.2.0 "$(aarch64-linux-gnu-g++ --version | head -1)"
expect_version clang-format 23.1.1 "$(clang-format --version)"
expect_version clang-tidy 23.1.1 "$(clang-tidy --version | grep -i 'version')"
expect_version cmake 4.4.3 "$(cmake --version | head -1)"
expect_version ninja 1.13.1 "$(ninja --version)"
vcpkg version --disable-metrics | head -1

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/hello.cpp" <<'EOF'
#include <expected>
#include <print>
#include <string>
#include <thread>

static std::expected<int, std::string> answer() { return 42; }

int main() {
  int value = 0;
  std::jthread t([&] { value = answer().value_or(0); });
  t.join();
  std::println("hello {}", value);
  return value == 42 ? 0 : 1;
}
EOF

flags=(-std=c++23 -O2 -Wall -Wextra -Werror -static-libstdc++ -static-libgcc)

g++ "${flags[@]}" -o "$work/hello-x86_64" "$work/hello.cpp"
[[ $("$work/hello-x86_64") == "hello 42" ]] || { echo "FAIL: native run" >&2; exit 1; }
echo "ok: native build and run"

aarch64-linux-gnu-g++ "${flags[@]}" -mcpu=cortex-a76 -s -o "$work/hello-aarch64" "$work/hello.cpp"
[[ $(qemu-aarch64 -L "$SYSROOT" "$work/hello-aarch64") == "hello 42" ]] ||
  { echo "FAIL: aarch64 run under qemu" >&2; exit 1; }
echo "ok: aarch64 cross build and qemu run"

needed=$(readelf -d "$work/hello-aarch64" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
while read -r lib; do
  case $lib in
    libc.so.6 | libm.so.6 | ld-linux-aarch64.so.1 | libpthread.so.0 | libdl.so.2 | librt.so.1) ;;
    *) echo "FAIL: non-glibc NEEDED entry: $lib" >&2; exit 1 ;;
  esac
done <<<"$needed"
echo "ok: NEEDED is glibc only ($(echo $needed))"

max_glibc=$(readelf -V --wide "$work/hello-aarch64" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1)
if [[ $(printf '%s\n%s\n' "$max_glibc" "$GLIBC_CEILING" | sort -V | tail -1) != "$GLIBC_CEILING" ]]; then
  echo "FAIL: requires GLIBC_$max_glibc, ceiling is $GLIBC_CEILING" >&2
  exit 1
fi
echo "ok: highest GLIBC_ version is $max_glibc (ceiling $GLIBC_CEILING)"

echo "toolchain smoke test passed"
