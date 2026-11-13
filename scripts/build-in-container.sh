#!/usr/bin/env bash
# Runs a build step inside the pinned toolchain image, with the repo mounted
# at /src. This is the only supported way to build class-t4.
#
#   scripts/build-in-container.sh <host-debug|rpi5-release|format-check|tidy>
#
# Environment:
#   TOOLCHAIN_IMAGE  image to use instead of docker/toolchain/DIGEST
#                    (local toolchain work only; CI and releases use DIGEST)
#   CLASS_T4_CACHE   host directory for vcpkg downloads, buildtrees and
#                    binary cache (default: <repo>/.cache)
#   CLASS_T4_WERROR  ON to build with -Werror (CI sets this)
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

usage() {
  echo "usage: $0 <host-debug|rpi5-release|format-check|tidy>" >&2
  exit 2
}

[[ $# -eq 1 ]] || usage
step=$1

image=${TOOLCHAIN_IMAGE:-}
if [[ -z $image ]]; then
  digest_file="$repo/docker/toolchain/DIGEST"
  [[ -s $digest_file ]] || { echo "error: $digest_file is missing or empty" >&2; exit 1; }
  image=$(tr -d '[:space:]' <"$digest_file")
  [[ $image == *@sha256:* ]] || { echo "error: DIGEST must be an image@sha256:... reference" >&2; exit 1; }
fi

cache=${CLASS_T4_CACHE:-$repo/.cache}
mkdir -p "$cache"

# Release reproducibility: the commit time stands in for a build time
# wherever a tool insists on one.
source_date_epoch=${SOURCE_DATE_EPOCH:-$(git -C "$repo" log -1 --format=%ct 2>/dev/null || echo 0)}

in_container() {
  local tty=()
  [[ -t 0 && -t 1 ]] && tty=(-it)
  docker run --rm "${tty[@]}" \
    --user "$(id -u):$(id -g)" \
    --volume "$repo:/src" \
    --volume "$cache:/cache" \
    --workdir /src \
    --env SOURCE_DATE_EPOCH="$source_date_epoch" \
    --env CLASS_T4_WERROR="${CLASS_T4_WERROR:-OFF}" \
    --env GIT_CONFIG_COUNT=1 \
    --env GIT_CONFIG_KEY_0=safe.directory \
    --env GIT_CONFIG_VALUE_0=/src \
    "$image" bash -euo pipefail -c "mkdir -p /cache/vcpkg/downloads /cache/vcpkg/archives; $1"
}

# Tracked C++ sources only, so build trees and vendored code are never touched.
cxx_files='git ls-files -z "*.cpp" "*.hpp" "*.h" "*.cc"'

case $step in
  host-debug)
    in_container '
      cmake --preset host-debug
      cmake --build --preset host-debug
      ctest --preset host-debug'
    ;;
  rpi5-release)
    in_container '
      cmake --preset rpi5-release
      cmake --build --preset rpi5-release
      ctest --preset rpi5-release
      scripts/check-release-binary.sh build/rpi5-release/app/server/class-t4'
    ;;
  format-check)
    in_container "$cxx_files | xargs -0 --no-run-if-empty clang-format --dry-run -Werror"
    ;;
  tidy)
    # clang-tidy reads the host-debug compile database, so configure it first.
    in_container "
      cmake --preset host-debug >/dev/null
      git ls-files -z '*.cpp' '*.cc' | xargs -0 --no-run-if-empty \
        clang-tidy -p build/host-debug --quiet --warnings-as-errors='*'"
    ;;
  *)
    usage
    ;;
esac
