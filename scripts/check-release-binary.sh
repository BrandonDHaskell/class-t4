#!/usr/bin/env bash
# Enforces the release linking rules on an aarch64 binary: NEEDED entries are
# glibc libraries only, and no symbol needs a GLIBC_ version above the
# epiphany ceiling (Debian 13, glibc 2.41). Also fails on embedded build or
# host paths, except the dependency build-config strings accepted in the
# static-linking ADR: OpenSSL's compiled-in engines/modules dirs and build
# command line ("openssl version -f"), and Drogon's compiled-in include flags. Those are fixed container paths, identical in CI
# and on lovelace, and are never used at runtime.
set -euo pipefail

readonly GLIBC_CEILING=2.41

[[ $# -eq 1 && -f $1 ]] || { echo "usage: $0 <binary>" >&2; exit 2; }
binary=$1

machine=$(readelf -h "$binary" | sed -n 's/^ *Machine: *//p')
[[ $machine == AArch64 ]] || { echo "FAIL: $binary is $machine, not AArch64" >&2; exit 1; }

status=0
while read -r lib; do
  case $lib in
    libc.so.6 | libm.so.6 | ld-linux-aarch64.so.1 | libpthread.so.0 | libdl.so.2 | librt.so.1 | libresolv.so.2) ;;
    *) echo "FAIL: non-glibc NEEDED entry: $lib" >&2; status=1 ;;
  esac
done < <(readelf -d "$binary" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')

max_glibc=$(readelf -V --wide "$binary" | grep -o 'GLIBC_[0-9][0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1)
if [[ $(printf '%s\n%s\n' "$max_glibc" "$GLIBC_CEILING" | sort -V | tail -1) != "$GLIBC_CEILING" ]]; then
  echo "FAIL: needs GLIBC_$max_glibc, ceiling is $GLIBC_CEILING" >&2
  status=1
fi

if readelf -S "$binary" | grep -q '\.debug_'; then
  echo "FAIL: $binary is not stripped, no build paths" >&2
  status=1
fi

leaks=$(strings -a "$binary" |
  grep -E '(^|[^.[:alnum:]_-])/(src|cache|home|root|opt/(gcc|gcc-cross|sysroot|vcpkg|llvm))(/|$)' |
  grep -vE '/src/build/vcpkg_installed/|/cache/vcpkg/packages/|^compiler: /opt/gcc-cross/bin/aarch64-linux-gnu-gcc ' || true)
if [[ -n $leaks ]]; then
  echo "FAIL: embedded build or host paths:" >&2
  echo "$leaks" | head -20 >&2
  status=1
fi

[[ $status -eq 0 ]] && echo "ok: $binary: NEEDED glibc only, max GLIBC_$max_glibc <= $GLIBC_CEILING, stripped, no build paths"
exit $status
