# class-t4: Project Plan

Nov 12, 2026 · @Brandon

## Status

class-t4 is further along than `main` shows: since Nov 11, `feat/m1-walking-skeleton` holds most of M0 plus M1 phases A and B and most of phase C (toolchain image pinned, CMake and presets, `core`, Drogon platform, `/health` and `/metrics`). `main` still holds only `README.md` and `LICENSE`. Next step: finish M0 so the governance files land on `main` and the M1 doc matches what was built.

| Item | State |
| --- | --- |
| Design | Agreed in [class-t4: Repo and Setup Reference](https://claude.ai/artifact/2tY7CKTNQ5hxxn6qnuVCPT) |
| Handoff files | On the M1 branch since Nov 11, not on main (`CLAUDE.md`, `.claude/settings.json`, `docs/milestones/`) |
| epiphany host | Built, 10.0.0.57, custom kernel 7.3.3-srv1 |
| crows-nest (ops) | Designed in [crows-nest: Ops Repo Reference](https://claude.ai/artifact/WhJBrshSSChTt8deaRsXft) |
| minisign key | Release key exists on lovelace and epiphany (\`/etc/minisign/release.pub\`, used for kernel packages); class-t4 key decision open |
| First release | None |

## Scope and success criteria

class-t4 owns the application, its build and release, and its deploy units on epiphany. Monitoring, backups, the CA and host agents live in crows-nest, so each epiphany success criterion maps to an owner.

| Success criterion (epiphany) | class-t4 delivers | crows-nest delivers | Milestone |
| --- | --- | --- | --- |
| Calendar aggregator over TLS on the LAN within 4GB | Server, calendar module, `/api/v1`, memory limits | CA-issued cert | M2, M4 |
| Reproducible, signed, health-gated releases with auto rollback | Toolchain, `release.yml`, sign scripts, install/activate/rollback | None | M1, M2 |
| Metrics, alerts and backups on tranquility | `/metrics` on 9464, structured logs, backup-safe DB snapshot | Prometheus, Alertmanager, log shipping, restic pull | M2, M5 |
| Tested restore | Restore runbook in `docs/runbooks/` | Restore drill job | M5 |

**Out of scope:** the SPA (own repo later), generic Pi 5 host setup (its own runbook), anything that writes back to calendars.

## Milestones

Six milestones take class-t4 from an empty repo to v1.0.0, and each one ships a tagged release that must pass its gate before the next starts.

&#91;embedded content: class-t4 roadmap · 6 milestones, each with an exit gate\]

M0 to M2 prove the build, sign and deploy pipeline with no features, so every later feature ships through a pipeline already proven on epiphany.

## Work breakdown

Each milestone ends with a tagged release, so progress is always something deployable. M1's full criteria live in `docs/milestones/class-t4 M1 Walking Skeleton.md`; each milestone also gets its own task doc, linked under its heading, when it starts.

### M0: Repo bootstrap

Audit, remaining tasks and validation checks: [class-t4 M0: Repo Bootstrap](https://claude.ai/artifact/Nz7XrHkzarrQ3XH5fWcAaA).

### M1: Walking skeleton (v0.1.0)

Task breakdown, specifications and test plan: [class-t4 M1: Walking Skeleton](https://claude.ai/artifact/FxnR3DCAFDrBDsFr6RqL6Z).

- [ ] Toolchain image (GCC 16.2 native + aarch64 cross, Debian 13 sysroot), `toolchain.yml`, digest pinned in `docker/toolchain/DIGEST`
- [ ] CMake presets, toolchain file, triplet, `vcpkg.json` with pinned baseline
- [ ] `core`, Drogon bootstrap, HTTPS on 8443, ops listener on 9464
- [ ] `/health` on both listeners, `/metrics` with `class_t4_build_info`
- [ ] Layer check in CI (no Drogon or SQLite includes below adapters)
- [ ] Unit and integration tests; aarch64 tests under qemu-user
- [ ] Deterministic tarball, `release.yml`, `build-release.sh`, `verify-and-sign.sh`
- [ ] First lovelace rebuild matches CI hashes byte for byte

### M2: Deploy to epiphany (v0.2.0)

Decisions, specifications, tasks and test plan: [class-t4 M2: Deploy to epiphany](https://claude.ai/artifact/2FAPZvoWWRfm413HDLN5a8).

### M3: Persistence and secrets (v0.3.0)

Decisions, specifications, tasks and test plan: [class-t4 M3: Persistence and Secrets](https://claude.ai/artifact/GtwM42FbMwnUAnUXMmyUgP).

### M4: Calendar aggregator (v0.4.0 to v0.6.0)

Decisions, specifications, tasks and test plan: [class-t4 M4: Calendar Aggregator](https://claude.ai/artifact/MtoEk1m5Jm8oyEqK5NodFu).

### M5: Operable and restorable (v1.0.0)

Decisions, specifications, tasks and test plan: [class-t4 M5: Operable and Restorable](https://claude.ai/artifact/L4uKGXHmG6xeTLMEPjJXgR).

## Dependencies

M0 and M1 depend on nothing outside lovelace and GitHub; crows-nest first gates class-t4 at M2.

**Joint order with crows-nest:** class-t4 M0 and M1 beside crows-nest M0 and M1; crows-nest M3 core (internal CA, `tls_cert`, deploy playbook, ports) before class-t4 M2 phase E; crows-nest M2 beside it; crows-nest M5 PR1 and PR2 (nightly pull) before class-t4 M3 phase E; crows-nest M4 and M5 PR3 before class-t4 M5.

| Needed by | What | From |
| --- | --- | --- |
| M0 | minisign key pair | Brandon, on lovelace |
| M1 | GHCR push for the toolchain image | GitHub Actions built-in token |
| M2 | Deploy playbook, host prerequisites and SSH from tranquility | crows-nest M3 (T23, T24) |
| M2 | LAN TLS cert and CA root on clients | crows-nest internal CA |
| M2 | Host firewall: inbound 8443 from the LAN, 9464 and 22 from tranquility only; no output chain (the app allowlist enforces egress) | crows-nest (host agents) |
| M3 | Token encryption key provisioned as a systemd credential | Brandon; off-host copy in the password manager, never in restic |
| M4 | Secret iCal address for each Google calendar (ICS provider); no OAuth client | Each Google calendar owner |
| M4 | Entra app with public client flows | Microsoft Entra |
| M4 | App-specific passwords per Apple owner | Each calendar owner |
| M5 | Prometheus, Alertmanager, log shipping, restic pull, restore drill | crows-nest |

## Risks

The two risks most likely to stall the plan are both in M1: the hand-built toolchain image and byte-for-byte reproducibility.

| Risk | Milestone | Mitigation |
| --- | --- | --- |
| GCC 16.2 cross build in the image is slow or fragile | M1 | Build once, push to GHCR, pin the digest; rebuild only on Dockerfile changes |
| lovelace and CI hashes differ | M1 | Treat the first mismatch as a bug; diff with diffoscope before adding features |
| Static OpenSSL misses security fixes | M2 onward | Scheduled CVE check job; patch releases bump the vcpkg baseline |
| Drogon thread pools and SQLite cache exceed the memory budget | M2, M5 | `MemoryMax` in the unit, thread counts in config, load test before v1.0.0 |
| Google's device flow cannot request calendar scopes | M4 | Google calendars sync through the ICS provider; a Google API provider is deferred past v1.0 |
| Losing the token key forces re-consent for every source | M3 | Key copy in the password manager, never in restic; the startup canary proves a restored key matches |
| tranquility on wifi drops scrapes and backups | M5 | Dead-man heartbeat in crows-nest; alert on stale backups |

## Open decisions

Seven decisions shape this plan; each has a proposed default to confirm or override.

- [ ] **class-t4 signing key:** reuse the existing release key or generate a dedicated class-t4 key. Default: dedicated key, so the kernel key never signs app releases.

* [ ] **Milestone shape:** confirm M0 to M5 as drafted, one tagged release per milestone. Default: yes.
* [ ] **Work tracking:** this doc only, or mirror milestones as GitHub Milestones and Issues. Default: GitHub Issues per task, this doc for the roadmap.
* [ ] **Memory budget:** the `MemoryMax` for class-t4 out of the 4GB. Default: 1GB hard cap, alert at 768MB.
* [ ] **Provider order in M4:** decided: ICS (Google included, through its secret iCal address), CalDAV, Microsoft.
* [ ] **TLS issuance:** decided: crows-nest's `tls_cert` role requests on epiphany, signs on tranquility and pushes the cert; class-t4 restarts through its health gate.
* [ ] **Reference doc refresh:** fold the Nov 11 decisions (GCC 16.2, glibc 2.41 target, ports 8443 and 9464, GHCR, static linking) and resolved open items into the reference doc before export. Default: yes, as part of M0.

Resolved since the reference doc: the ops repo is crows-nest, the generic Pi 5 runbook lives in its own doc, and the CA lives on tranquility.

## Change log

| Date | Change |
| --- | --- |
| 2026-11-12 | Plan created from the repo state and the reference doc. minisign tooling confirmed in place on lovelace and epiphany |
| 2026-11-12 | Cross-repo review applied: superseded checklists removed; Google moves to the ICS provider and M4 ends at v0.6.0; token key copy lives in the password manager; TLS issuance and provider order decided; joint order with crows-nest added |
