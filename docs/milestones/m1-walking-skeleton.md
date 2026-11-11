# M1: Walking skeleton

## Goal

A tagged `v0.1.0` that builds reproducibly in the pinned toolchain, passes tests on x86_64 and aarch64, and produces a deterministic aarch64 tarball. The server runs, serves `/health` and `/metrics`, and does nothing else yet.

## Out of scope

- Calendar module, SQLite usage, OAuth, providers
- Deploying to epiphany (install, activate, rollback scripts are M2)
- Anything in crows-nest

## Acceptance criteria

### Toolchain
- [ ] `docker/toolchain/Dockerfile` on `debian:trixie` pinned by digest
- [ ] GCC 16.2 native x86_64 and `aarch64-linux-gnu` cross, built in the image
- [ ] Debian 13 arm64 sysroot (glibc 2.41)
- [ ] Clang 23.1.1 `clang-format` and `clang-tidy`; CMake, Ninja, vcpkg at a pinned commit; `qemu-user` for aarch64 tests
- [ ] `.github/workflows/toolchain.yml` builds and pushes the image to GHCR on Dockerfile changes
- [ ] The resulting digest is recorded in `docker/toolchain/DIGEST`, and every other build uses only that digest

### Build
- [ ] `CMakePresets.json` with `host-debug` and `rpi5-release`
- [ ] `cmake/toolchains/aarch64-rpi5.cmake` and `triplets/arm64-linux-rpi5.cmake` (with `-mcpu=cortex-a76` and `-ffile-prefix-map`)
- [ ] `vcpkg.json` (Drogon, GoogleTest) with the baseline pinned
- [ ] `scripts/build-in-container.sh` supporting `host-debug`, `rpi5-release`, `package`, `format-check`, `tidy`; repo mounted at `/src`
- [ ] Release binary: `readelf -d` shows only glibc `NEEDED` entries
- [ ] Release binary: the highest `GLIBC_` symbol version is at most 2.41

### Architecture
- [ ] `app/core/` target with no Drogon or SQLite dependency
- [ ] `app/platform/drogon/`: bootstrap, HTTPS listener on 8443, ops listener on 9464
- [ ] `app/server/`: `main.cpp` composition root, module registry (empty), CLI with a `serve` subcommand
- [ ] CI check that fails on any `#include <drogon` or `#include <sqlite3` outside `adapters/` and `app/platform/`

### Behavior
- [ ] Loads config from a path given on the command line; `config/class-t4.example.json` documents every key
- [ ] `/health` on both listeners returns 200 with a small JSON body
- [ ] `/metrics` on 9464 in Prometheus text format, including `class_t4_build_info{version,commit}`
- [ ] TLS cert and key paths come from config. Dev and tests generate a throwaway self-signed cert at runtime; no certs committed
- [ ] Graceful shutdown on SIGTERM

### Tests
- [ ] GoogleTest unit tests for `core`
- [ ] `tests/integration/` starts the host binary, hits `/health` on both listeners and `/metrics`, then sends SIGTERM and checks a clean exit

### CI
- [ ] `.github/workflows/ci.yml`: both presets, unit tests natively and under qemu-user, format check, clang-tidy, include check, all with `-Werror`
- [ ] `.github/workflows/release.yml`: on `v*` tag, builds `rpi5-release`, packages, and creates a draft GitHub release with the tarball and `SHA256SUMS`

### Packaging and release
- [ ] `packaging/` produces `class-t4-<version>-aarch64.tar.gz` (binary, systemd unit, example config, LICENSE, NOTICE)
- [ ] Running packaging twice from a clean tree produces identical SHA-256
- [ ] `scripts/release/build-release.sh` builds a tag in the pinned image on lovelace
- [ ] `scripts/release/verify-and-sign.sh` fetches the draft release's `SHA256SUMS`, compares, then calls `minisign -S` and uploads the `.minisig` only on a match. Test it with a throwaway key in a temp directory only

### Deploy artifacts (drafted, not deployed)
- [ ] `deploy/systemd/class-t4.service`: `User=class-t4`, a `LoadCredential=` placeholder, and hardening (`ProtectSystem=strict`, `ProtectHome=yes`, `NoNewPrivileges=yes`, `PrivateTmp=yes`, `ReadWritePaths=/var/lib/class-t4`)

### Docs and repo files
- [ ] `docs/ops-contract.md`: ports, `/health` and `/metrics` paths, service user, config, cert and key paths, data directory, and a planned backup command (marked as M3)
- [ ] `docs/adr/0001-record-architecture-decisions.md` and ADRs for the toolchain and static linking choices
- [ ] `README.md`, `LICENSE` (Apache 2.0), `NOTICE`, `CHANGELOG.md`, `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitignore`

### Done when
- [ ] CI is green on `main`
- [ ] Tag `v0.1.0` produces a draft release
- [ ] Brandon runs `build-release.sh` and `verify-and-sign.sh` on lovelace, and the hashes match

## Suggested order

1. Toolchain image and `toolchain.yml`, then pin the digest
2. CMake skeleton, presets, vcpkg, `build-in-container.sh`
3. `core`, server bootstrap, listeners, `/health`, `/metrics`
4. Unit and integration tests
5. `ci.yml`
6. Packaging and the determinism check
7. `release.yml` and the release scripts
8. Docs, ADRs, contract

Commit after each step. Stop and ask if a criterion can't be met as written.