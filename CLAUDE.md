# class-t4

C++23 backend for **epiphany** (Raspberry Pi 5, 4GB, Debian 13 trixie, glibc 2.41), built on Drogon with SQLite. The first service is a read-only household calendar aggregator, LAN-only. The repo is a modular monolith: one binary, many modules, and Drogon and SQLite are adapters, not the architecture.

Full design reference: `docs/design.md`. Current milestone: `docs/milestones/`.

## Commands

Every build runs inside the pinned toolchain container, with the repo mounted at `/src`. Never build with host compilers.

- Pull toolchain: `docker pull ghcr.io/brandondhaskell/class-t4-toolchain@<digest>` (digest pinned in `docker/toolchain/DIGEST`)
- Host build + unit tests: `scripts/build-in-container.sh host-debug`
- Cross build: `scripts/build-in-container.sh rpi5-release`
- Package: `scripts/build-in-container.sh package`
- Format check: `scripts/build-in-container.sh format-check`
- Lint: `scripts/build-in-container.sh tidy`

## Toolchain

- Base image: `debian:trixie`, pinned by digest.
- Compiler: GCC 16.2, native x86_64 and `aarch64-linux-gnu` cross, built in the image.
- Clang tools: Clang 23.1.1 for `clang-format` and `clang-tidy` only.
- Sysroot: Debian 13 arm64 packages (glibc 2.41). Never link against a newer glibc than 2.41.
- Linking: `-static-libstdc++ -static-libgcc`, with all vcpkg dependencies static. The release binary's `NEEDED` entries must be glibc libraries only.
- vcpkg: manifest mode, baseline pinned in `vcpkg-configuration.json`, custom triplet `triplets/arm64-linux-rpi5.cmake` (`-mcpu=cortex-a76`).
- Presets: `host-debug` (native, tests, sanitizers) and `rpi5-release` (cross, release artifact).

## Architecture rules

- Layers per module: `domain`, `application`, `ports`, `adapters`. Shared kernel in `app/core/`.
- `core`, `domain`, `application` and `ports` must never include or link Drogon, SQLite, or any HTTP or TLS library. This is enforced by CMake targets and a CI include check. Never loosen these to make something compile; fix the dependency direction instead.
- Adapters depend inward on ports. Nothing depends on an adapter.
- `app/server/` is the only composition root.
- Each module owns its schema and migrations.

## Network contract

- HTTPS API: port **8443** (TLS, Drogon), routes under `/api/v1/`.
- Ops listener: port **9464**, plain HTTP, serves `/metrics` (Prometheus text format) and `/health`.
- `/health` also exists on 8443, so TLS probes check liveness.
- Any change to ports, paths or endpoints must also update `docs/ops-contract.md`.

## Style

- `clang-format` with LLVM base style (`.clang-format`).
- Naming: `snake_case` for types, functions and variables; `UPPER_CASE` for macros; `PascalCase` for template parameters.
- Drogon's camelCase API appears only inside `adapters/http/` and `app/platform/drogon/`.
- Warnings: `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`; `-Werror` in CI.
- Headers with `#pragma once`. C++23, no modules.
- Tests: GoogleTest. Unit tests live in each module's `tests/`; whole-binary tests in `tests/integration/`.

## Reproducibility

- Release artifacts must be byte-identical between CI and lovelace.
- Use `SOURCE_DATE_EPOCH` (tag commit time), `-ffile-prefix-map=/src=.`, stripped binaries, and tarballs built with sorted names, fixed mtime, owner and group 0, numeric owner.
- Never embed timestamps, hostnames, usernames or absolute paths in artifacts.
- Do not change the toolchain digest or the vcpkg baseline unless asked.

## Writing

- No em dashes anywhere: docs, comments, commit messages. Use colons, commas, parentheses or hyphens.
- Conventional Commits. SemVer from `vX.Y.Z` git tags.
- Record non-trivial decisions as ADRs in `docs/adr/`.

## Hard rules

- No secrets in the repo, ever. Config files in the repo are `*.example.json` with placeholders.
- Never read, create, copy or modify anything in `~/.minisign/`. Release signing is done by Brandon. Script tests may use a throwaway key generated in a temp directory.
- Never connect to epiphany (10.0.0.57) or tranquility. Deployment is out of scope until a milestone says otherwise.
- Never install packages on the lovelace host. Toolchain changes go in `docker/toolchain/`.