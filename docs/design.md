# class-t4: Repo and Setup Reference

Nov 10, 2026 · @Brandon

## Summary

class-t4 is the C++23 Drogon backend for epiphany (Raspberry Pi 5, 4GB). Its first service is a read-only household calendar aggregator, served over TLS on the LAN only.

| Area | Decision |
| --- | --- |
| Repositories | class-t4 (app + epiphany deploy); separate ops repo for tranquility |
| Build host | lovelace, cross-compiling to aarch64 |
| Toolchain | Container image pinned by digest, recent compilers, vcpkg manifest with pinned baseline |
| Architecture | Modular monolith: one binary under `app/`, framework-agnostic core |
| API and UI | JSON API under `/api/v1`; decoupled SPA later, likely its own repo |
| Language | C++23, headers (no modules for now) |
| Tests | GoogleTest |
| CI | GitHub Actions, public repo |
| Release artifact | Versioned tarball + systemd unit, symlink swap for rollback |
| Signing | minisign, signed on lovelace only after CI hashes match |
| Calendar sources | Google, Microsoft, Apple, ICS feeds; read-only aggregation |
| OAuth | Device code flow, run on epiphany via the class-t4 CLI |
| License and versioning | Apache 2.0, SemVer |

## Repositories

Two repos keep the app host and the ops host separate.

| Repo | Holds | Visibility |
| --- | --- | --- |
| class-t4 | Application source, build, packaging, epiphany deploy units and scripts, epiphany-specific runbooks | Public |
| Ops repo (name TBD) | tranquility configs: metrics, alerting, backup jobs targeting epiphany | TBD |

- **Name:** class-t4, after the Dakar Rally class for stripped-down SSVs.
- **License:** Apache 2.0, with `LICENSE` and `NOTICE` at the root.
- **Versioning:** SemVer. The git tag `vX.Y.Z` is the single source of truth and CMake reads it.
- **Not in class-t4:** the generic Pi 5 host runbook, since it is reused across future hosts.

## Build and toolchain

lovelace cross-compiles to aarch64 inside a pinned container, and CI runs the same image.

- **Container:** `docker/toolchain/Dockerfile`, base image pinned by digest, recent GCC/Clang pinned by version.
- **Dependencies:** vcpkg manifest (`vcpkg.json`) with the baseline pinned in `vcpkg-configuration.json`. Drogon comes in through vcpkg, so the Debian base on epiphany stays pristine.
- **Triplet:** custom `triplets/arm64-linux-rpi5.cmake` with `-mcpu=cortex-a76`.
- **Cross toolchain file:** `cmake/toolchains/aarch64-rpi5.cmake`.

| Preset | Target | Used for |
| --- | --- | --- |
| `host-debug` | Native x86\_64 on lovelace | Fast unit test loop, sanitizers |
| `rpi5-release` | aarch64 cross build | Release artifacts; tests run under qemu-user in CI |

CI (GitHub Actions) builds and tests both presets in the pinned image on every push. Because the repo is public, arm64 runners are free if native arm64 CI is ever wanted.

## Project structure

The tree below is the agreed layout, including the calendar providers, the CLI and the release scripts.

```
class-t4/
├── .github/workflows/
│   ├── ci.yml                  # build + test both presets in the pinned image
│   └── release.yml             # tag-triggered: draft release, tarball, SHA256SUMS
├── app/
│   ├── core/                   # shared kernel: Result, clock, config, logging ports (no Drogon)
│   ├── modules/
│   │   └── calendar/
│   │       ├── domain/         # events as read-only projections (source + owner)
│   │       ├── application/
│   │       │   └── sync/       # incremental sync engine, per-source sync state
│   │       ├── ports/
│   │       ├── adapters/
│   │       │   ├── http/       # Drogon controllers, /api/v1/calendar
│   │       │   ├── sqlite/     # repository implementations
│   │       │   └── providers/
│   │       │       ├── google/     # Google Calendar API, OAuth
│   │       │       ├── microsoft/  # Microsoft Graph, OAuth
│   │       │       ├── caldav/     # Apple iCloud, generic CalDAV
│   │       │       └── ics/        # subscribed feeds
│   │       ├── migrations/     # module-owned schema
│   │       └── tests/
│   ├── platform/
│   │   ├── drogon/             # bootstrap, TLS, /health, /metrics
│   │   ├── sqlite/             # connection, migration runner
│   │   ├── http_client/        # outbound HTTPS behind a port
│   │   ├── oauth/              # device flow, token refresh and rotation
│   │   └── secrets/            # key loading via systemd credentials
│   └── server/
│       ├── main.cpp            # composition root, module registry
│       └── cli/                # serve | migrate | source add|list|remove
├── tests/integration/          # whole-binary tests against a running server
├── cmake/
│   ├── toolchains/aarch64-rpi5.cmake
│   └── modules/                # warnings, sanitizers, version-from-tag, reproducibility flags
├── triplets/arm64-linux-rpi5.cmake
├── docker/toolchain/Dockerfile
├── deploy/
│   ├── systemd/class-t4.service
│   └── scripts/                # install (minisign verify), activate (swap + health gate), rollback
├── packaging/                  # deterministic tarball, manifest, checksums
├── keys/class-t4.pub           # minisign public key
├── scripts/
│   ├── build-in-container.sh
│   └── release/
│       ├── build-release.sh    # same image, same command as CI
│       └── verify-and-sign.sh  # compare to CI SHA256SUMS, sign, upload .minisig
├── config/class-t4.example.json
├── docs/
│   ├── adr/
│   └── runbooks/               # epiphany-specific only
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── vcpkg-configuration.json
├── .clang-format  .clang-tidy  .editorconfig  .gitignore
├── LICENSE  NOTICE
├── CHANGELOG.md
└── README.md
```

## Architecture boundaries

The CMake target graph enforces that Drogon and SQLite are adapters, not the architecture.

- Each layer is its own CMake target: `core`, then per module `domain`, `application`, `ports`, `adapters`.
- `core`, `domain` and `application` must not link Drogon, SQLite or any HTTP library. A violation is a link error, not a review comment.
- Adapters depend inward on ports. Nothing depends outward on an adapter.
- `server/` is the only place that wires modules, adapters and platform services together.
- Each module owns its schema and migrations. New services become new directories under `app/modules/`.

## Calendar sources and sync

The app only reads. Each owner keeps managing their own calendar, and the aggregate is visible on the home network only.

| Source | Protocol | Auth | Scope |
| --- | --- | --- | --- |
| Google | Google Calendar API | OAuth device code flow | `calendar.readonly` |
| Microsoft | Microsoft Graph (no CalDAV option) | OAuth device code flow | `Calendars.Read` |
| Apple iCloud | CalDAV | App-specific password | Read only by usage |
| ICS feeds | HTTPS GET | None or URL token | Read only |

- **No write-back:** no conflict resolution and no origin tracking for edits.
- **Events are projections:** each is tagged with its source and owner for display.
- **Incremental sync:** per-source sync state lives in `application/sync/`.
- **Rebuildable cache:** the event cache in SQLite can always be rebuilt from the providers.
- **Egress:** epiphany stays LAN-only inbound but needs outbound 443 to Google, Microsoft and Apple. The host firewall allows only that.

## OAuth and credentials on epiphany

All consent runs on epiphany through the class-t4 CLI, so tokens are created where they are used and never cross hosts.

**Adding a source**

1. SSH to epiphany and run `class-t4 source add google --owner <name>` (or `microsoft`).
2. The CLI prints a verification URL and a short code.
3. The calendar owner approves on their own phone, signed in to their own account.
4. The CLI polls the provider's token endpoint and stores the refresh token encrypted.
5. For Apple, `class-t4 source add caldav --owner <name>` prompts for the owner's app-specific password instead.

**Why this design**

- No redirect URI, no loopback listener and no browser on the server, which avoids the LAN hostname redirect problem.
- The CLI only makes outbound calls, so it adds no inbound surface to epiphany.
- tranquility never holds calendar credentials, which keeps role separation intact.

**Storage**

- Refresh tokens rotate, so they live encrypted in SQLite under `/var/lib/class-t4/`.
- The encryption key comes from systemd `LoadCredential=` and never sits in `/etc` config files.

**Provider setup**

- [ ] Google: create an OAuth client of type "TVs and Limited Input devices". A Desktop-type client is rejected by the device endpoint.
- [ ] Google: move the OAuth app out of "Testing" status, or refresh tokens expire after about 7 days.
- [ ] Microsoft: in the Entra app registration, set "Allow public client flows" to Yes.
- [ ] Apple: each owner generates an app-specific password for their Apple ID.

Google's device flow allowlist includes `calendar.readonly` ([source](https://docs.rs/crate/grr-cli/1.0.1/source/docs/gcp-setup.md)).

## Release, signing and reproducibility

A release is signed only when lovelace reproduces CI's artifact byte for byte. This keeps the minisign secret key off GitHub and doubles as the reproducibility check.

1. Push tag `vX.Y.Z`.
2. `release.yml` builds `rpi5-release` in the pinned image and creates a draft GitHub release with the tarball and `SHA256SUMS`.
3. On lovelace, `scripts/release/build-release.sh` builds the same tag with the same image and command.
4. `scripts/release/verify-and-sign.sh` fetches CI's `SHA256SUMS` and compares.
5. On a match, it signs with minisign, uploads the `.minisig` and publishes the release. On a mismatch, it stops and the release stays a draft.

**Determinism requirements**

- Toolchain image pinned by digest; vcpkg baseline pinned.
- `SOURCE_DATE_EPOCH` set from the tag commit time.
- `-ffile-prefix-map` (and `-fdebug-prefix-map`) to strip build paths.
- Stripped release binaries.
- Tarballs with sorted names, fixed mtime, owner and group 0, numeric owner.

## Deployment on epiphany

Each release unpacks into its own directory, and activation is an atomic symlink swap behind a health gate.

| Path | Contents |
| --- | --- |
| `/opt/class-t4/releases/<ver>/` | Unpacked release tarball |
| `/opt/class-t4/current` | Symlink to the active release |
| `/etc/class-t4/` | Config and `minisign.pub` |
| `/var/lib/class-t4/` | SQLite DB, including encrypted tokens |
| systemd credentials | Token encryption key via `LoadCredential=` |

1. **Install:** `deploy/scripts/install` verifies the `.minisig` against `/etc/class-t4/minisign.pub`, then unpacks.
2. **Activate:** `activate` swaps `current`, restarts the unit and waits for `/health` to pass.
3. **Rollback:** if the health gate fails, `rollback` swaps back to the previous release automatically.

**What backups to tranquility must protect:** source config, encrypted tokens and the encryption key. The event cache can be rebuilt. Without the key, the fallback is re-consent for every source.

## Open items

- [ ] Name the ops repo for tranquility.
- [ ] Decide where the generic Pi 5 host runbook lives (outside class-t4).
- [ ] Choose the TLS certificate source for LAN serving.
- [ ] Decide how the token encryption key is backed up alongside the DB.
- [ ] Decouple the SPA into its own repo when the time comes; until then, keep `/api/v1` stable.
