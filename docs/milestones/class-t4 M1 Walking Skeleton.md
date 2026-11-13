# class-t4 M1: Walking Skeleton

Nov 12, 2026 · @Brandon

## Overview

M1 ships `v0.1.0`: an empty server that builds reproducibly in the pinned toolchain, passes tests on x86\_64 and aarch64, and produces a signed, deterministic aarch64 tarball. It serves `/health` and `/metrics` and nothing else. Roadmap context: [class-t4: Project Plan](https://claude.ai/artifact/Eoy2RvQxbejN5U8tmEtY6J).

**Status note (Nov 12):** phases A, B and most of C are already built on `feat/m1-walking-skeleton`, and some of it differs from the specifications below (config keys, script names, CLI parser). This doc is reconciled with the branch in [M0 task M0-4](https://claude.ai/artifact/Nz7XrHkzarrQ3XH5fWcAaA); until then, the branch is the more accurate source.

**Exit criteria**

- CI is green on `main` across build, test, lint, include and determinism jobs.
- `v0.1.0-rc.1` and `v0.1.0` each produce a draft release with the tarball and `SHA256SUMS`.
- lovelace rebuilds `v0.1.0` byte for byte, and the published `.minisig` verifies against `keys/class-t4.pub`.
- Every test in the test plan below exists and passes where it is meant to run.

**Entry criteria (from M0)**

- [ ] `CLAUDE.md`, `.claude/settings.json` and `docs/milestones/` committed
- [ ] `docs/design.md` exported from the refreshed reference doc
- [ ] Specifications below confirmed
- [ ] class-t4 signing key decided (needed by G4, not at kickoff)

**In scope:** toolchain image, build system, `core`, Drogon platform layer, two listeners, `/health`, `/metrics`, tests, CI, packaging, release workflow, signing scripts, a drafted systemd unit, docs and ADRs.

**Out of scope:** the calendar module, SQLite, OAuth, providers, any deploy to epiphany (M2), anything in crows-nest.

## Specifications

These are the facts Claude Code builds against; everything marked proposed needs your confirmation before kickoff. Changes to any of them after kickoff also update `docs/ops-contract.md`.

### Network contract (agreed)

| Listener | Port | Protocol | Paths |
| --- | --- | --- | --- |
| API | 8443 | HTTPS (TLS from config) | `/health`, `/api/v1/*` (none in M1) |
| Ops | 9464 | Plain HTTP | `/health`, `/metrics` |

`/metrics` is served on the ops listener only; on 8443 it returns 404 (proposed).

### Config file (proposed)

Passed with `--config <path>`. Unknown keys are rejected, so a typo fails fast instead of silently using a default.

```json
{
  "listen": {
    "api": { "address": "0.0.0.0", "port": 8443 },
    "ops": { "address": "0.0.0.0", "port": 9464 }
  },
  "tls": {
    "cert_path": "/etc/class-t4/tls/cert.pem",
    "key_path": "/etc/class-t4/tls/key.pem"
  },
  "log": { "level": "info" },
  "threads": 2,
  "data_dir": "/var/lib/class-t4"
}
```

| Key | Rule |
| --- | --- |
| `listen.*.port` | 1 to 65535; the two ports must differ |
| `listen.*.address` | IPv4 or IPv6 literal |
| `tls.cert_path`, `tls.key_path` | Required; files must exist and load at startup |
| `log.level` | `trace`, `debug`, `info`, `warn` or `error` |
| `threads` | 1 to 4 (Drogon IO threads) |
| `data_dir` | Reserved for M3; accepted, not used |

### `/health` (proposed)

`200`, `Content-Type: application/json`, same body on both listeners:

```json
{"status":"ok","version":"0.1.0","commit":"0123456789ab"}
```

### Metrics (proposed)

Prometheus text exposition format on `GET :9464/metrics`.

| Metric | Type | Labels | Purpose |
| --- | --- | --- | --- |
| `class_t4_build_info` | gauge, always 1 | `version`, `commit` | Which build is running |
| `process_resident_memory_bytes` | gauge | none | Memory budget tracking |
| `process_start_time_seconds` | gauge | none | Restart detection |
| `class_t4_http_requests_total` | counter | `listener`, `route`, `code` | Traffic and errors |

### Logging (proposed)

One JSON object per line on stdout; journald captures it on epiphany and M5 ships it to tranquility.

```json
{"ts":"2026-11-12T19:15:00.123Z","level":"info","msg":"listening","listener":"api","port":8443}
```

### CLI and exit codes (proposed)

| Command | Behavior |
| --- | --- |
| `class-t4 serve --config <path>` | Runs the server until SIGTERM or SIGINT |
| `class-t4 --version` | Prints `<version> (<commit>)` |

Exit codes: `0` clean shutdown, `1` runtime failure, `2` usage or config error. Parser: CLI11 from vcpkg.

### Versioning (proposed)

Read from `git describe` at configure time; `commit` is the 12-character SHA.

| Checkout | Version string |
| --- | --- |
| Tag `v0.1.0` | `0.1.0` |
| Tag `v0.1.0-rc.1` | `0.1.0-rc.1` |
| Untagged | `0.0.0-dev+<sha12>` |

### Release tarball (proposed)

`class-t4-<version>-aarch64.tar.gz`, deterministic, one top-level directory:

```
class-t4-<version>/
├── bin/class-t4
├── share/systemd/class-t4.service
├── share/config/class-t4.example.json
├── LICENSE
└── NOTICE
```

`SHA256SUMS` lists the tarball in `sha256sum` format.

## Execution plan

29 tasks in 8 phases, about 14 working sessions of roughly 2 hours; you own 4 tasks and review 8 PRs.

- **Workflow:** one task = one GitHub issue and one Conventional Commit; one PR per phase, merged by you after CI passes.
- **Tracking:** GitHub Milestone `M1`, issues labeled `phase:A` to `phase:H`. Status rolls up to the project plan weekly.
- **Claude Code:** runs locally on lovelace, works one phase at a time, and stops at every task owned by you.
- **Critical path:** A1 → A6 → B3 → C2 → D2 → E1/E2 → G1 → G4. Phase H runs in parallel from C onward.

| Phase | Tasks | Sessions | Your touchpoints | Exits with |
| --- | --- | --- | --- | --- |
| A Toolchain | A1 to A6 | 3 | Approve A1 pins, run A6 | Pinned image digest |
| B Build skeleton | B1 to B5 | 1 | PR review | Both presets build |
| C App skeleton | C1 to C5 | 2 | PR review | Server answers on both ports |
| D Tests | D1, D2 | 2 | PR review | Unit and integration suites green |
| E CI | E1 to E4 | 1 | PR review, E4 | Required checks on `main` |
| F Packaging | F1 to F3 | 2 | PR review | Deterministic tarball |
| G Release | G1 to G4 | 2 | PR review, G4 | Signed `v0.1.0` |
| H Docs | H1 to H3 | 1 | PR review | Contract and ADRs merged |

Task format below: **ID. Name** · size · owner · depends on, then deliverables, implementation details and validation.

## Tasks

### A: Toolchain image

Phase A produces one image, pinned by digest, that every later build uses on CI and lovelace alike.

**A1. Pin toolchain versions** · S · Claude, you approve · depends on M0

- **Deliverables:** `docker/toolchain/versions.env`.
- **Details:** one line per input with version and SHA-256 or digest: `debian:trixie` base digest, snapshot.debian.org timestamp for all apt packages, GCC 16.2 and binutils source tarballs, Clang 23.1.1, CMake, Ninja, vcpkg commit, qemu-user, minisign, gh CLI. The Dockerfile reads only from this file.
- **Validate:** you review the list; A4's smoke script compares installed versions against it.

**A2. Native GCC 16.2 stage** · M · Claude · A1

- **Deliverables:** `docker/toolchain/Dockerfile` (multi-stage), stage `gcc-native`.
- **Details:** build from the pinned tarball, `--enable-languages=c,c++ --disable-multilib --disable-bootstrap`, prefix `/opt/gcc-16.2`. Bootstrap is skipped to cut build time; the shipped compiler is still 16.2.
- **Validate:** `smoke.sh` compiles and runs a C++23 program using `std::expected` and `std::print`.

**A3. aarch64 cross stage and sysroot** · M · Claude · A2

- **Deliverables:** stages `sysroot` and `gcc-cross` in the Dockerfile.
- **Details:** sysroot at `/opt/sysroot-aarch64` from Debian 13 arm64 `libc6`, `libc6-dev` and `linux-libc-dev` at the pinned snapshot (glibc 2.41). binutils and GCC 16.2 with `--target=aarch64-linux-gnu --with-sysroot=/opt/sysroot-aarch64`.
- **Validate:** `smoke.sh` cross-builds a C++ hello with `-static-libstdc++ -static-libgcc`; `readelf -h` shows AArch64, `readelf -d` shows only glibc `NEEDED` entries, and the highest `GLIBC_` version from `objdump -T` is 2.41 or lower.

**A4. Tool layer and smoke script** · S · Claude · A3

- **Deliverables:** final Dockerfile stage, `docker/toolchain/smoke.sh`.
- **Details:** adds `clang-format` and `clang-tidy` 23.1.1, CMake, Ninja, vcpkg cloned at the pinned commit and bootstrapped, `qemu-user`, `minisign`, `gh`, `systemd` (for unit verification), git. `VCPKG_ROOT` and `PATH` set in the image.
- **Validate:** `smoke.sh` fails if any tool version differs from `versions.env`, and runs the A3 hello under `qemu-aarch64`.

**A5. `toolchain.yml` workflow** · S · Claude · A4

- **Deliverables:** `.github/workflows/toolchain.yml`.
- **Details:** triggers on pushes to `main` touching `docker/toolchain/**` and on `workflow_dispatch`. Builds with buildx, runs `smoke.sh` inside the built image, then pushes `ghcr.io/brandondhaskell/class-t4-toolchain:sha-<commit>` with `GITHUB_TOKEN` (`packages: write`). Writes the digest to the job summary.
- **Validate:** workflow green; the image pulls by digest on lovelace. **Fallback:** if the job exceeds the 6-hour runner limit or runs out of disk, build on lovelace and push with a token stored only on lovelace.

**A6. Pin the digest** · S · **you** · A5

- **Deliverables:** `docker/toolchain/DIGEST`; GHCR package visibility set to public.
- **Details:** copy the digest from the A5 job summary. Public visibility lets CI and lovelace pull without credentials.
- **Validate:** `docker pull ghcr.io/brandondhaskell/class-t4-toolchain@$(cat docker/toolchain/DIGEST)` succeeds while logged out.

### B: Build skeleton

Phase B makes both presets configure and build an empty target inside the pinned image, with the binary checks in place before any app code exists.

**B1. Root CMake and modules** · S · Claude · A6

- **Deliverables:** `CMakeLists.txt`, `cmake/modules/{warnings,sanitizers,version,reproducibility}.cmake`.
- **Details:** C++23 with extensions off. Warnings `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror=date-time`; `-Werror` behind option `CLASS_T4_WERROR` (on in CI). ASan and UBSan on `host-debug`. `version.cmake` turns `git describe` into a generated `build_info.hpp` per the versioning spec. Reproducibility module adds `-ffile-prefix-map=/src=.`.
- **Validate:** `tests/cmake/version_test.cmake` (run by CTest) checks tag, rc tag and untagged cases against fixtures.

**B2. Presets, toolchain file, triplets** · S · Claude · B1

- **Deliverables:** `CMakePresets.json`, `cmake/toolchains/aarch64-rpi5.cmake`, `triplets/arm64-linux-rpi5.cmake`, `triplets/x64-linux-static.cmake`.
- **Details:** configure, build and test presets for `host-debug` (Ninja, Debug, sanitizers, tests) and `rpi5-release` (Release, vcpkg chainloading the cross toolchain file). The toolchain file sets the sysroot and `CMAKE_CROSSCOMPILING_EMULATOR` to `qemu-aarch64 -L /opt/sysroot-aarch64`, so CTest runs aarch64 tests under qemu. Triplets are static libraries, dynamic CRT, with `-mcpu=cortex-a76` on arm64.
- **Validate:** both presets configure and report GCC 16.2 as the compiler.

**B3. vcpkg manifest** · M · Claude · B2

- **Deliverables:** `vcpkg.json`, `vcpkg-configuration.json`.
- **Details:** dependencies `drogon` (minimal features: no ORM backends, no Redis), `gtest`, `cli11`. Baseline = the vcpkg commit from A1. Binary cache at `.vcpkg-cache/` (gitignored). Record the resolved Drogon and OpenSSL versions in the PR description.
- **Validate:** both triplets install; the installed tree contains no `.so` files.

**B4. `build-in-container.sh`** · S · Claude · B3

- **Deliverables:** `scripts/build-in-container.sh`.
- **Details:** `docker run --rm` with the repo at `/src`, your UID and GID, and the image by digest from `DIGEST` only. Subcommands: `host-debug`, `rpi5-release`, `package`, `format-check`, `tidy`. Passes `SOURCE_DATE_EPOCH` through.
- **Validate:** every subcommand exits 0 on a clean tree; the script exits non-zero when `DIGEST` is missing.

**B5. Binary checks** · S · Claude · B4

- **Deliverables:** `scripts/check-binary.sh`, `tests/scripts/check_binary_test.sh`.
- **Details:** fails unless the ELF is AArch64, every `NEEDED` entry is in {`libc.so.6`, `libm.so.6`, `ld-linux-aarch64.so.1`}, and the highest `GLIBC_` symbol version is 2.41 or lower.
- **Validate:** the test passes on a correctly linked binary and fails on two planted bad ones: one dynamically linked to `libstdc++.so.6`, one built for x86\_64.

### C: App skeleton

Phase C puts all logic that can be pure into `core`, so most behavior is unit-tested without Drogon, and keeps Drogon to a thin platform layer.

**C1. `core` library** · M · Claude · B5

- **Deliverables:** `app/core/` target: `result.hpp`, config types and validation, logging port and JSON-lines formatter, metrics registry and text exposition writer, health body builder; `app/platform/config/` JSON loader.
- **Details:** `result<T>` wraps `std::expected<T, error>` with an error code and message. Config validation implements every rule in the spec table. Parsing JSON lives in `platform/config` (jsoncpp, already pulled in by Drogon), so `core` depends only on the standard library. The metrics writer escapes label values per the exposition format.
- **Validate:** D1 unit tests; `core` has no entries in `LINK_LIBRARIES` (asserted at configure time).

**C2. Drogon platform layer** · M · Claude · C1

- **Deliverables:** `app/platform/drogon/`: bootstrap, listeners, request accounting, log bridge, shutdown.
- **Details:**
  - Two `addListener` calls from config; TLS cert and key only on the API listener; `setThreadNum` from `threads`.
  - Drogon routes are global across listeners, so a pre-routing filter reads the request's local port and returns 404 for `/metrics` on the API port.
  - A post-handling advice increments `class_t4_http_requests_total` with the matched route pattern; unmatched paths use `route="other"` to cap label cardinality.
  - RSS from `/proc/self/statm` at scrape time; start time recorded at startup.
  - trantor's logger output is redirected into the JSON-lines formatter.
  - SIGTERM and SIGINT stop the event loop and the process exits 0.
- **Validate:** D2 integration tests.

**C3. `/health` and `/metrics` handlers** · S · Claude · C2

- **Deliverables:** handlers in `app/platform/drogon/`.
- **Details:** `/health` returns the spec body with `application/json` on both listeners. `/metrics` returns `text/plain; version=0.0.4; charset=utf-8` on the ops listener only.
- **Validate:** D2 integration tests.

**C4. Composition root and CLI** · S · Claude · C3

- **Deliverables:** `app/server/main.cpp`, `app/server/cli/`, empty module registry.
- **Details:** startup order: parse args, load and validate config, start logging, build the module registry (empty in M1), start Drogon. CLI11 subcommand `serve --config <path>` (required) and `--version`. Exit codes per the spec.
- **Validate:** D2 tests for `--version`, missing config and invalid config.

**C5. Layer check** · S · Claude · C1

- **Deliverables:** `scripts/check-layers.sh`, `tests/scripts/check_layers_test.sh` with fixture trees.
- **Details:** fails on any include of `drogon/`, `trantor/`, `sqlite3`, `openssl/` or `json/` under `app/core/` or any module's `domain/`, `application/` or `ports/`.
- **Validate:** the test passes on the real tree and fails on a fixture with `#include <drogon/drogon.h>` in `core`.

### D: Tests

Phase D builds the two suites the test plan lists: unit tests that run on both architectures, and whole-binary tests on `host-debug`.

**D1. Unit tests for `core`** · S · Claude · C1

- **Deliverables:** `app/core/tests/`, one GoogleTest executable registered with CTest.
- **Details:** covers tests U-01 to U-12 in the test plan. No network, no files outside a temp directory, no sleeps.
- **Validate:** green under `host-debug` (with ASan and UBSan) and under `rpi5-release` through qemu.

**D2. Integration harness** · M · Claude · C4

- **Deliverables:** `tests/integration/` with a `server_process` GoogleTest fixture.
- **Details:**
  - Generates a throwaway CA and server cert with OpenSSL into a temp directory per test run; nothing is committed.
  - Picks two free ports by binding port 0, writes a config file, and starts the built binary with `posix_spawn`.
  - Waits up to 10 seconds for `/health` on the ops port, then runs the test.
  - The HTTPS client trusts only the generated CA, so the TLS path is tested for real.
  - Teardown sends SIGTERM, waits up to 5 seconds and asserts exit 0. Captured stdout must parse as JSON line by line.
- **Validate:** tests I-01 to I-09 green on `host-debug` with sanitizers. They do not run under qemu (decision 3).

### E: CI

Phase E runs every check through `build-in-container.sh`, so CI and lovelace execute the same commands in the same image.

Shared settings for `.github/workflows/ci.yml`: triggers on pull requests and pushes to `main`; `permissions: contents: read`; checkout with `fetch-depth: 0` so `git describe` sees tags; one concurrency group per ref with cancel-in-progress; every action pinned by commit SHA, with Dependabot keeping the pins current; `actions/cache` on `.vcpkg-cache/` keyed on `vcpkg.json`, `vcpkg-configuration.json`, `triplets/**`, `DIGEST` and the preset.

**E1. `host-debug` job** · S · Claude · D2

- **Details:** build with `-Werror`, run all CTest tests (unit and integration) under ASan and UBSan, upload CTest logs on failure.
- **Validate:** green on the phase E PR; a planted failing assertion turns it red.

**E2. `rpi5-release` job** · S · Claude · D1, B5

- **Details:** cross build, run CTest excluding the `integration` label (unit tests under qemu), run `check-binary.sh`, upload the stripped binary as a workflow artifact.
- **Validate:** green; the job log shows the unit tests executing through `qemu-aarch64`.

**E3. Lint job** · S · Claude · C5

- **Details:** `format-check`, `tidy` (blocking per the spec unless decided otherwise; see open items), `check-layers.sh`, every script test under `tests/scripts/`, and `actionlint` on the workflows, and the pinned gitleaks scan from M0-12.
- **Validate:** green on the real tree; a planted misformatted file turns it red.

**E4. Branch protection** · S · **you** · E1 to E3, F2

- **Details:** on `main`: pull request required, status checks E1, E2, E3 and F2 required, no force pushes, rebase merging so each task's commit survives.
- **Validate:** a throwaway PR with a failing test cannot be merged; close it afterward.

### F: Packaging

Phase F turns the release binary into a tarball whose SHA-256 depends only on the source commit, which is what makes the lovelace signing check possible.

**F1. Deterministic tarball** · S · Claude · E2

- **Deliverables:** `packaging/package.sh`, called by `build-in-container.sh package`; output in `dist/` (gitignored).
- **Details:** `SOURCE_DATE_EPOCH` = commit time of `HEAD` (`git log -1 --format=%ct`). Strip with `aarch64-linux-gnu-strip --strip-all`. Stage the spec layout with modes 0755 for the binary and 0644 for everything else. `tar --sort=name --mtime=@$SOURCE_DATE_EPOCH --owner=0 --group=0 --numeric-owner`, then `gzip -n -9`. Write `SHA256SUMS`.
- **Validate:** S-05 checks the listing against the spec layout, owners, modes and mtimes.

**F2. Determinism job** · S · Claude · F1

- **Deliverables:** `determinism` job in `ci.yml`.
- **Details:** two clean `rpi5-release` builds and packages from the same commit (build directories removed between runs, vcpkg cache kept), then compare SHA-256. On a mismatch, upload both tarballs so you can diff them with diffoscope.
- **Validate:** green; a planted step that writes the current time into the staged tree turns it red.

**F3. Draft systemd unit** · S · Claude · F1

- **Deliverables:** `deploy/systemd/class-t4.service`, `tests/scripts/unit_verify_test.sh`.
- **Details:** `User=class-t4`, `ExecStart=/opt/class-t4/current/bin/class-t4 serve --config /etc/class-t4/config.json`, `Restart=on-failure`, a commented `LoadCredential=` placeholder for M3, a commented `MemoryMax=` placeholder for M2. Hardening: `ProtectSystem=strict`, `ProtectHome=yes`, `NoNewPrivileges=yes`, `PrivateTmp=yes`, `ReadWritePaths=/var/lib/class-t4`, `RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX`.
- **Validate:** the test points `ExecStart` at the unpacked tarball's binary and runs `systemd-analyze verify`; it must pass. Record the `systemd-analyze security` score in the PR as a baseline.

### G: Release

Phase G connects tag, CI draft, lovelace rebuild and signature, with the secret key touched only by you.

**G1. `release.yml`** · S · Claude · F2

- **Deliverables:** `.github/workflows/release.yml`.
- **Details:** triggers on tags matching `v*`; `permissions: contents: write`. Builds and packages through `build-in-container.sh`, runs the binary's `--version` under qemu and fails if it does not match the tag, then creates a draft release with the tarball and `SHA256SUMS`. Tags with `-rc.` are marked prerelease.
- **Validate:** W-03 in the test plan (rc tag produces a two-asset draft).

**G2. `build-release.sh`** · S · Claude · F1

- **Deliverables:** `scripts/release/build-release.sh <tag>`.
- **Details:** clones the tag fresh into a temp directory (never builds from your working tree), runs the same `rpi5-release` and `package` commands as CI, prints the tarball path and SHA-256. Refuses if the tag does not exist.
- **Validate:** S-07; on the rc tag its hash equals CI's.

**G3. `verify-and-sign.sh`** · S · Claude · G1, G2

- **Deliverables:** `scripts/release/verify-and-sign.sh <tag>`, `tests/scripts/verify_and_sign_test.sh`.
- **Details:**
  1. Downloads the draft's `SHA256SUMS` with `gh release download`.
  2. Compares it to the local `build-release.sh` output; on a mismatch exits 3 and signs nothing.
  3. On a match, signs with `minisign -S` using a trusted comment of `class-t4 <tag>`, then self-checks with `minisign -V` against `keys/class-t4.pub`.
  4. Uploads the `.minisig` and publishes the release.
  5. `--dry-run` stops before step 4.

  The build and the hash comparison run in the image. Signing runs on the host with Debian's `minisign`, so the key is never mounted into a container; keep the key on removable encrypted media, mounted only to sign. `GH_TOKEN` is passed from lovelace (open item 9).
- **Validate:** S-08 uses a throwaway key in a temp directory and a stub `gh` on `PATH`; Claude Code never reads `~/.minisign/`.

**G4. rc dry run, then `v0.1.0`** · S · **you** · G1 to G3, E4

- **Details:** follow the release procedure under Acceptance and sign-off, first for `v0.1.0-rc.1`, then for `v0.1.0`.
- **Validate:** M-01 and M-02 in the test plan.

### H: Docs

Phase H runs alongside C to G and records what was built, so the contract crows-nest depends on is written down before M2.

**H1. `docs/ops-contract.md`** · S · Claude · C3

- **Details:** ports and listeners, `/health` body, metric names and labels, log format, service user, config path and keys, cert and key paths, data directory, exit codes, and the planned backup command (marked M3).
- **Validate:** S-09 checks that every port, path and metric name in the contract appears in the integration tests.

**H2. ADRs** · S · Claude · B3

- **Deliverables:** `docs/adr/0001-record-architecture-decisions.md`, `0002-modular-monolith.md`, `0003-cross-toolchain-and-pinning.md`, `0004-release-signing-flow.md`, `0005-static-linking.md`.
- **Details:** each states context, decision and consequences; 0004 states that CI and lovelace share one toolchain image, so the rebuild check covers the build job and not the toolchain; 0005 records the OpenSSL patching cost and the M2 CVE check.
- **Validate:** your PR review.

**H3. Repo files** · S · Claude · any

- **Deliverables:** `README.md` (purpose, build commands, links), `CHANGELOG.md` (Keep a Changelog format), `NOTICE`, `.clang-tidy`, `.editorconfig`, `.gitignore`.
- **Validate:** E3 lint passes with the committed `.clang-format` and `.clang-tidy`.

## Test plan

37 tests prove M1: 12 unit, 9 integration, 9 script, 4 workflow and 3 manual. Every one is automated except the manual tests, which need your signing key or repo admin rights.

### Unit tests (GoogleTest; `host-debug` with sanitizers and `rpi5-release` under qemu)

| ID | Test | Proves | Task |
| --- | --- | --- | --- |
| U-01 | Example config parses to the expected values | `class-t4.example.json` stays valid | C1 |
| U-02 | Missing `tls.cert_path` fails, naming the key | Required keys enforced | C1 |
| U-03 | Unknown key fails | Typos fail fast | C1 |
| U-04 | Ports 0 and 65536 fail | Port range enforced | C1 |
| U-05 | Same port for API and ops fails | Listeners cannot collide | C1 |
| U-06 | Log level `verbose` fails | Level enum enforced | C1 |
| U-07 | `threads` 0 and 5 fail | Thread bounds enforced | C1 |
| U-08 | Health body is valid JSON with `status`, `version`, `commit` | Spec body shape | C1 |
| U-09 | `class_t4_build_info` renders exactly per the spec | Exposition format | C1 |
| U-10 | Label values with `"`, `\` and newline are escaped | Scrapes never break | C1 |
| U-11 | Counter increments render per label set | Request accounting | C1 |
| U-12 | Log line is one line of valid JSON, control characters escaped, UTC timestamp with milliseconds | Log format | C1 |

### Integration tests (`tests/integration/`, `host-debug` only)

The fixture also asserts, for every test, that all stdout lines parse as JSON with `ts`, `level` and `msg`.

| ID | Test | Proves | Task |
| --- | --- | --- | --- |
| I-01 | HTTPS `GET /health` on the API port returns 200 and the spec body; the client trusts only the test CA | TLS listener works end to end | C2, C3 |
| I-02 | HTTP `GET /health` on the ops port returns the same body | Ops liveness | C3 |
| I-03 | `GET /metrics` on ops returns the content type and `build_info` matching `--version` | Metrics endpoint and build stamping | C3 |
| I-04 | `GET /metrics` on the API port returns 404 | Metrics stay off the LAN API | C2 |
| I-05 | After 5 API health calls, the counter for `listener="api",route="/health",code="200"` is at least 5 | Request accounting is wired | C2 |
| I-06 | RSS is above 0; start time falls inside the test's window | Process metrics are real | C2 |
| I-07 | SIGTERM exits 0 within 5 seconds | Graceful shutdown | C2 |
| I-08 | Invalid config, missing cert file and missing `--config` each exit 2 | Startup errors are clear | C4 |
| I-09 | `--version` prints `<version> (<commit>)` and exits 0 | CLI contract | C4 |

### Script tests (CTest or the E3 lint job)

| ID | Test | Proves | Task |
| --- | --- | --- | --- |
| S-01 | `version_test.cmake`: tag, rc tag and untagged fixtures | Versioning spec | B1 |
| S-02 | `check_binary_test.sh`: good binary passes; dynamic `libstdc++` and x86\_64 fail | Link rules hold | B5 |
| S-03 | `check_layers_test.sh`: real tree passes; Drogon include in `core` fails | Layer rule is enforced | C5 |
| S-04 | `build-in-container.sh` exits non-zero without `DIGEST` | Builds only use the pinned image | B4 |
| S-05 | Tarball listing matches the spec layout, owner 0, modes and mtime | Package shape and determinism inputs | F1 |
| S-06 | `unit_verify_test.sh`: `systemd-analyze verify` passes | Unit is valid before M2 | F3 |
| S-07 | `build-release.sh` fails on a missing tag; a dirty working tree does not change its hash | Release builds come from the tag only | G2 |
| S-08 | `verify_and_sign_test.sh`: match signs and verifies; mismatch exits 3 with no `.minisig`; missing draft fails; `--dry-run` uploads nothing | Signing gate is safe | G3 |
| S-09 | Every port, path and metric in `ops-contract.md` appears in the integration tests | Contract matches what is tested | H1 |

### Workflow tests (GitHub Actions)

| ID | Test | Proves | Task |
| --- | --- | --- | --- |
| W-01 | `toolchain.yml` runs `smoke.sh` in the built image before pushing | Image is correct before anyone uses it | A4, A5 |
| W-02 | `ci.yml` jobs green on `main`, including two-build hash equality | Build, test, lint and determinism | E1 to E3, F2 |
| W-03 | `v0.1.0-rc.1` produces a prerelease draft with tarball and `SHA256SUMS`; version check passes | Release workflow | G1 |
| W-04 | One-time planted failures: failing assertion (E1), misformatted file (E3), timestamp in the tree (F2) each turn CI red | The gates can actually fail | E1, E3, F2 |

### Manual tests (you)

| ID | Test | Proves | Task |
| --- | --- | --- | --- |
| M-01 | rc release procedure: lovelace hash equals CI, release published, downloaded tarball verifies against `keys/class-t4.pub` in a clean directory | End-to-end signed release | G4 |
| M-02 | Same procedure for `v0.1.0` | Milestone release | G4 |
| M-03 | Throwaway PR with a failing test cannot merge | Branch protection | E4 |

## Acceptance and sign-off

M1 is done when every box below is ticked and you have signed `v0.1.0`; you are the sole approver.

**Definition of done**

- [ ] All 29 tasks closed in GitHub Milestone `M1`
- [ ] All 37 tests in the test plan exist and pass where they are meant to run
- [ ] `docs/ops-contract.md`, ADRs 0001 to 0005 and `CHANGELOG.md` merged
- [ ] `v0.1.0` published with tarball, `SHA256SUMS` and `.minisig`
- [ ] Baselines recorded in the project plan change log

**Release procedure (rc first, then final)**

1. Confirm `main` is green and `CHANGELOG.md` has an entry for the version.
2. `git tag -a v0.1.0-rc.1 -m "v0.1.0-rc.1" && git push origin v0.1.0-rc.1`.
3. Wait for `release.yml` to create the prerelease draft (W-03).
4. On lovelace: `scripts/release/build-release.sh v0.1.0-rc.1`.
5. `scripts/release/verify-and-sign.sh v0.1.0-rc.1 --dry-run` and confirm the hashes match.
6. Run it again without `--dry-run` and enter your minisign password.
7. From a clean directory: `gh release download v0.1.0-rc.1`, then `sha256sum -c SHA256SUMS` and `minisign -Vm class-t4-0.1.0-rc.1-aarch64.tar.gz -p keys/class-t4.pub`.
8. Repeat steps 1 to 7 for `v0.1.0`.

**Baselines to record:** stripped binary size, tarball size, toolchain image size, duration of each CI job (cold and cached).

## Risks

Phase A carries most of M1's schedule risk; everything after it is ordinary build work.

| Risk | Phase | Trigger | Response |
| --- | --- | --- | --- |
| Toolchain build exceeds the 6-hour runner limit or disk | A | A5 job times out or fails on space | Build on lovelace, push with a lovelace-only token |
| Drogon or its deps fail to cross-build statically | B | B3 install fails for `arm64-linux-rpi5` | Overlay port or feature trim; record in ADR 0003 |
| Per-listener routing is awkward in Drogon | C | I-04 cannot pass cleanly | Fall back to a per-listener path check inside the handler |
| Hash mismatch between CI and lovelace | F, G | F2 or M-01 fails | diffoscope both tarballs; fix before tagging `v0.1.0` |
| qemu runs flaky or slow | E | E2 is intermittent or over 15 minutes | Mark the slow tests, keep the rest blocking |
| Scope creep from M2 | All | Deploy or epiphany work appears in a PR | Reject; `settings.json` already blocks SSH |

## Open items

Five items stand between this doc and kickoff; each has a default this doc already assumes.

- [ ] **Confirm the proposed specifications** (config, `/health`, metrics, logging, CLI, versioning, tarball). Default: accept as written.
- [ ] **clang-tidy gate:** blocking from day one (current spec) or report-only until the end of M1. Default: report-only until H3 lands, then blocking.
- [x] **Signing key:** decided: dedicated `class-t4.key` (M0 D3); G3 uses `keys/class-t4.pub`.
- [ ] **GitHub token on lovelace for G3:** fine-grained, `class-t4` contents read and write only, passed as `GH_TOKEN`. Default: yes.
- [ ] **Capacity and target date:** your weekly hours for class-t4 and a target for `v0.1.0`. Needed to date the phases.

Once these close, this doc is the spec Claude Code works from.

## Change log

| Date | Change |
| --- | --- |
| 2026-11-12 | Created from the M1 spec, the plan review and the task breakdown. Status note added after the branch audit; reconciliation pending (M0-4) |
| 2026-11-12 | Cross-repo review applied: signing moves to the host with the key on removable media; actions pinned by SHA; gitleaks in the lint job; ADR 0004 states the shared toolchain image limit |
| 2026-11-12 | `docs/milestones/m1-walking-skeleton.md` removed; this doc is the canonical M1 spec |
