# class-t4 M2: Deploy to epiphany

Nov 12, 2026 · @Brandon

## Overview

M2 ships `v0.2.0` running on epiphany: the M1 server installed from a signed release, served over TLS with a cert from the crows-nest CA, behind a health gate that rolls back on its own. No features are added; M2 proves the path every later feature ships through. Roadmap context: [class-t4: Project Plan](https://claude.ai/artifact/Eoy2RvQxbejN5U8tmEtY6J).

**Exit criteria**

- `v0.2.0` is active on epiphany, deployed by the documented path, and survives a reboot.
- A deliberately broken release is rolled back automatically, and the previous version keeps serving.
- A LAN client that trusts the internal CA root gets a valid TLS `/health`; tranquility can scrape `/metrics`; other hosts cannot.
- A cert rotation by crows-nest restarts the service through the health gate.
- The weekly CVE scan runs and has a proven failure path.
- Every test in the test plan passes.

**Entry criteria**

- [ ] M1 done: `v0.1.0` signed and published
- [ ] Decisions D1 to D8 confirmed
- [ ] crows-nest slice ready (phase D): firewall, the internal CA with `tls_cert`, the deploy playbook, ports and host prerequisites (crows-nest M3 T23, T24)

**Ownership** (from the crows-nest contract)

| class-t4 owns | crows-nest owns |
| --- | --- |
| Binary, release tarball, systemd unit, service user definition, app config, `class-t4-deploy`, `docs/ops-contract.md` | Base hardening, nftables firewall, TLS cert issuance and delivery, deploy orchestration from tranquility, node\_exporter, journal upload, backup user |

**Out of scope:** SQLite, secrets and `LoadCredential=` (M3), monitoring rules, dashboards, log shipping and backups (M5).

## Deploy flow

Every release is verified twice (on tranquility, then on epiphany) and only stays if the health gate passes; otherwise the previous release comes back without anyone touching the host.

&#91;embedded content: deploy flow · 4 hosts, 2 gates, 3 outcomes\]

The gate passes when both listeners answer `/health` with the expected version three times in a row within 30 seconds and the unit has not restarted.

## Decisions

Eight decisions shape M2; D1 matters most because it decides which repo does the deploying.

| # | Decision | Recommended | Alternatives | Blocks |
| --- | --- | --- | --- | --- |
| D1 | What drives a deploy | Ansible playbook in crows-nest, run on demand from tranquility | SSH from lovelace; epiphany pulls from GitHub | D4, E1 |
| D2 | Service identity | Static `class-t4` system user from a `sysusers.d` file in the tarball; `StateDirectory=class-t4` for `/var/lib/class-t4` | `DynamicUser=yes` | A1, A2 |
| D3 | Who owns epiphany's app config | `deploy/epiphany/class-t4.json` in class-t4, shipped in the tarball, installed by `class-t4-deploy` | Templated by crows-nest | A3, B1 |
| D4 | Memory limits | `MemoryHigh=768M`, `MemoryMax=1G` | Measure first, set in M5 | A1 |
| D5 | How cert changes reach the service | crows-nest's `tls_cert` role calls `class-t4-deploy restart`, which goes through the health gate | `systemctl reload` (unsupported: Drogon has no cert hot reload) | B4, D3 |
| D6 | CVE scanning | Weekly workflow: CycloneDX SBOM of the vcpkg dependencies, scanned with Grype pinned by digest; high or critical findings open a `security` issue | Manual OpenSSL advisory checks | C3 |
| D7 | Rollback drill artifact | `0.2.0-drill.1`, built from a `drill/fail-health` branch, signed by you, staged on tranquility only, never published | Rely on automated tests only | E2 |
| D8 | Names on epiphany's cert | epiphany's LAN address only (crows-nest decision 31); a `home.arpa` name is added when local DNS exists | Hostname and DNS names now | D3 |

**Reasoning**

- **D1:** it matches your kernel pattern (signed on lovelace, staged on tranquility, verified on the Pi). It also keeps epiphany credentials off lovelace, which crows-nest already requires because lovelace will run agentic workloads.
- **D2:** a static user gives the M5 backup user a stable group to read the database through; `DynamicUser` makes file ownership change between boots.
- **D3:** the config is not secret, so keeping it in the app repo means a config change ships, gets gated and rolls back exactly like a code change.

## Specifications

These assume the recommended decisions; paths and names follow the existing `docs/ops-contract.md` on the M1 branch. All of them move into the ops contract in task D1.

### On-host layout

| Path | Owner and mode | Written by | Contents |
| --- | --- | --- | --- |
| `/opt/class-t4/releases/<version>/` | root, read-only (0555 dirs, 0444 files, 0555 binary) | `class-t4-deploy install` | Unpacked tarball |
| `/opt/class-t4/current`, `/opt/class-t4/previous` | root symlinks | `class-t4-deploy activate` | Active and last-known-good release |
| `/usr/local/sbin/class-t4-deploy` | root 0755 | Bootstrap, then self-update | Deploy tool |
| `/etc/class-t4/minisign.pub` | root 0644 | Bootstrap (you) | Release verification key |
| `/etc/class-t4/class-t4.json` | root:class-t4 0640 | `class-t4-deploy` | App config |
| `/etc/class-t4/tls/server.crt`, `server.key` | root 0644; key root:class-t4 0640 | crows-nest `tls_cert` | API listener cert and key |
| `/var/lib/class-t4/` | class-t4 0750 | systemd (`StateDirectory=`) | Empty until M3 |
| `/var/cache/class-t4-deploy/` | root 0700 | Deploy playbook | Incoming tarballs and signatures |

### Service identity

`share/sysusers.d/class-t4.conf` in the tarball, installed to `/usr/lib/sysusers.d/`:

```
u class-t4 - "class-t4 service" /var/lib/class-t4 /usr/sbin/nologin
```

### systemd unit

| Group | Settings |
| --- | --- |
| Run | `ExecStart=/opt/class-t4/current/bin/class-t4 serve --config /etc/class-t4/class-t4.json`, `User=class-t4`, `Group=class-t4`, `StateDirectory=class-t4` |
| Restart | `Restart=on-failure`, `RestartSec=2`, `StartLimitIntervalSec=60`, `StartLimitBurst=3` |
| Memory | `MemoryHigh=768M`, `MemoryMax=1G` |
| Hardening | `ProtectSystem=strict`, `ProtectHome=yes`, `PrivateTmp=yes`, `PrivateDevices=yes`, `NoNewPrivileges=yes`, `CapabilityBoundingSet=`, `RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX`, `SystemCallFilter=@system-service`, `MemoryDenyWriteExecute=yes`, `LockPersonality=yes`, `ProtectKernelTunables=yes`, `ProtectKernelModules=yes`, `ProtectControlGroups=yes`, `RestrictNamespaces=yes` |
| Target | `systemd-analyze security` exposure 2.5 or lower |

No capabilities are needed: both ports are above 1024.

### `class-t4-deploy`

One POSIX shell script, run as root on epiphany. Every command takes the lock `/run/lock/class-t4-deploy.lock` and logs to the journal with tag `class-t4-deploy`.

| Command | Behavior |
| --- | --- |
| `install <tarball> <minisig>` | Verify with `/etc/class-t4/minisign.pub`; check the tarball name, top-level directory and trusted comment agree on the version; unpack read-only into `releases/<version>`. On first install, also install sysusers, unit and config. Same version twice is a no-op |
| `activate <version>` | Point `previous` at the current target, swap `current` atomically (`ln -s` to a temp name, then `mv -T`), restart, run the health gate; on failure, roll back automatically |
| `rollback` | Swap `current` and `previous`, restart, gate against the previous version |
| `restart` | Restart and gate the current version (used by `tls_cert`) |
| `status` | Current and previous versions, unit state, last gate result |
| `prune` | Keep `current`, `previous` and one more; delete the rest |

`activate` also refreshes `/usr/local/sbin/class-t4-deploy` from the new release, only after the gate passes. Config from the release replaces the installed one only with `--update-config`, and the old file is kept for rollback.

| Exit code | Meaning |
| --- | --- |
| 0 | Success |
| 2 | Usage error |
| 3 | Verification failed; nothing changed |
| 4 | Nothing to roll back to |
| 5 | Another deploy holds the lock |
| 10 | Activation failed and was rolled back; previous version serving |
| 11 | Rollback also failed; service needs attention |

### Health gate

Pass when, within 30 seconds of the restart, three consecutive 1-second polls all succeed:

- `GET http://127.0.0.1:9464/health` returns 200 with `version` equal to the expected version (so a still-running old process cannot pass);
- `GET https://127.0.0.1:8443/health` returns 200 (liveness only; cert validity is checked by blackbox\_exporter in crows-nest);
- the unit is `active` and its `NRestarts` count has not changed.

### Firewall needs (for crows-nest)

| Port | From | Purpose |
| --- | --- | --- |
| 8443/tcp | LAN subnet | API and TLS `/health` |
| 9464/tcp | tranquility only | Metrics scrape and probes |
| 22/tcp | tranquility only | Deploy playbook |

Everything else inbound is dropped. There is no output chain: egress is enforced by the app's allowlist from M3.

## Execution plan

23 tasks in 6 phases: about 13 sessions in class-t4 plus crows-nest M0, M1 and M3 core (about 21 hours of your review time there). The crows-nest slice is the likely long pole, so it should start as soon as D1 is confirmed, in parallel with phases A to C.

| Phase | Tasks | Sessions | Your touchpoints | Exits with |
| --- | --- | --- | --- | --- |
| A Host integration | A1 to A4 | 2 | PR review | Unit, sysusers, config and tarball v2 |
| B Deploy tool | B1 to B5 | 4 | PR review | `class-t4-deploy` with a passing test suite |
| C Toolchain and CI | C1 to C3 | 2 | Re-pin digest (C1) | Deploy tests and CVE scan in CI |
| D Contract and crows-nest slice | D1 to D4 | 0.5 here, \~5 in crows-nest | crows-nest work | Firewall, certs and deploy playbook ready |
| E First deploy and drills | E1 to E4 | 3 | All four | Proven deploy, rollback and cert rotation |
| F Docs and release | F1 to F3 | 1.5 | F3 | `v0.2.0` signed and running |

- **Critical path (class-t4):** A1 → A4 → B1 → B2 → B5 → C2 → E1.
- **Critical path (crows-nest):** D2 → D3 → D4 → E1.
- **Parallel:** C3 can start right after M1; F1 runbooks are written during phase E.
- **Workflow:** unchanged from M1 (one issue and commit per task, one PR per phase). Claude Code still never connects to epiphany; on-host steps are yours or the playbook's.

Task format below: **ID. Name** · size · owner · depends on, then details and validation.

## Tasks

### A: Host integration files

Phase A turns the M1 draft unit into the real one and adds everything else the host needs from the release.

**A1. Final systemd unit** · S · Claude · M1, D2, D4

- **Deliverables:** `deploy/systemd/class-t4.service` (replaces the M1 draft).
- **Details:** every setting in the unit specification; the M1 `LoadCredential=` placeholder stays commented for M3.
- **Validate:** S-01 (`systemd-analyze verify`), S-02 (exposure 2.5 or lower).

**A2. sysusers.d file** · S · Claude · D2

- **Deliverables:** `deploy/sysusers.d/class-t4.conf`.
- **Details:** the single line in the service identity spec.
- **Validate:** S-03 runs `systemd-sysusers --root=<tmp>` and checks the user, home and shell.

**A3. epiphany config** · S · Claude · D3

- **Deliverables:** `deploy/epiphany/class-t4.json`.
- **Details:** `https` on `0.0.0.0:8443` with `cert_file` and `key_file` under `/etc/class-t4/tls/`, `ops` on `0.0.0.0:9464`, `threads` 2, `log_level` `info`. No secrets.
- **Validate:** U-01 parses it with the same loader the server uses.

**A4. Tarball layout v2** · S · Claude · A1 to A3, B1

- **Deliverables:** updated `packaging/` and the M1 layout test.
- **Details:** adds `share/deploy/class-t4-deploy`, `share/sysusers.d/class-t4.conf` and `share/config/epiphany.json` to the M1 layout; the unit stays at `share/systemd/class-t4.service`.
- **Validate:** S-04 (layout, modes) and the M1 determinism job stay green.

### B: The `class-t4-deploy` tool

Phase B builds the one script that runs on epiphany, written so every path can be tested off-host: its root directory, `systemctl` and HTTP probe are injectable through environment variables (`CLASS_T4_ROOT`, `CLASS_T4_SYSTEMCTL`, `CLASS_T4_HTTP_GET`).

**B1. `install`** · M · Claude · A1 to A3

- **Deliverables:** `deploy/scripts/class-t4-deploy` (install path).
- **Details:**
  1. Take the lock; fail with exit 5 if held.
  2. `minisign -Vm <tarball> -x <minisig> -p /etc/class-t4/minisign.pub`; exit 3 on failure.
  3. Require the file name `class-t4-<version>-aarch64.tar.gz`, the trusted comment `class-t4 v<version>` and the top-level directory `class-t4-<version>/` to agree.
  4. List the archive first and reject absolute paths, `..`, devices and symlinks that point outside the release.
  5. Unpack into `releases/.tmp-<version>`, set read-only modes, then rename into place.
  6. First install only: install the sysusers file and run `systemd-sysusers`, install the unit to `/etc/systemd/system/`, `daemon-reload`, `enable`, and install the config if none exists.
- **Validate:** T-01 to T-06, T-14.

**B2. `activate` and the health gate** · M · Claude · B1

- **Details:** record the current target as `previous`; if the release's unit differs from the installed one, install it and `daemon-reload` (keeping the old unit for rollback); swap `current` atomically; restart; run the gate exactly as specified. On a pass, refresh the installed `class-t4-deploy` from the release and log the result. On a fail, roll back, point \`previous\` at its pre-activation target again, and exit 10.
- **Validate:** T-07 to T-09, T-15.

**B3. `rollback`** · S · Claude · B2

- **Details:** swap `current` and `previous`, restore the matching unit and config if they changed, restart, gate against the previous version. Exit 4 if there is no `previous`, 11 if the gate fails again (unit left running whichever release it has; journal entry at `crit` priority).
- **Validate:** T-10, T-11.

**B4. `restart`, `status`, `prune`** · S · Claude · B2

- **Details:** `restart` gates the current version and exits 10 if it fails (no rollback: nothing was swapped). `status` prints versions, unit state and the last gate result. `prune` is deferred past v1.0, along with T-13.
- **Validate:** T-12, T-13, T-16.

**B5. Deploy test suite** · M · Claude · B1 to B4

- **Deliverables:** `tests/deploy/run.sh` and fixtures.
- **Details:** builds fixture tarballs whose `bin/class-t4` is a stub script, signs them with a throwaway minisign key in a temp directory, runs every command against a temp root with a stub `systemctl` (records calls, simulates `NRestarts`) and a stub HTTP probe driven by the fixture. No root, network or real systemd needed.
- **Validate:** T-01 to T-16 pass in CI (task C2).

### C: Toolchain and CI

Phase C adds the three tools the deploy tests need to the image, wires the tests into CI, and adds the weekly CVE scan that static linking makes necessary.

**C1. Image additions** · S · Claude, **you** re-pin · M1

- **Details:** add `minisign`, `shellcheck` and `systemd` (for `systemd-analyze` and `systemd-sysusers` offline) from the pinned snapshot; extend `smoke-test.sh` to check them. `minisign` is also needed by M1's `verify-and-sign.sh` tests, so if M1 G3 lands first this is already done.
- **Validate:** W-01; you update `docker/toolchain/DIGEST`.

**C2. Deploy checks in CI** · S · Claude · B5, C1

- **Details:** the lint job runs `tests/deploy/run.sh`, `shellcheck` on everything under `deploy/` and `scripts/`, and S-01 to S-04.
- **Validate:** green; a planted bug that skips the gate's version check turns T-08 red.

**C3. Weekly CVE scan** · M · Claude · M1

- **Deliverables:** `.github/workflows/cve-scan.yml`, `scripts/sbom.sh`.
- **Details:**
  - Runs weekly, on `workflow_dispatch` and after each release tag.
  - `sbom.sh` writes a CycloneDX JSON SBOM of every vcpkg package in the release build (name, version, and CPE where one exists, OpenSSL included).
  - Grype, pinned by image digest, scans it and fails on high or critical findings.
  - On failure it opens or updates one issue labeled `security` (`permissions: issues: write`).
  - The SBOM is also attached to each GitHub release.
- **Validate:** W-02 uses a fixture SBOM with a known-vulnerable OpenSSL version in a dry-run mode that does not open issues.

### D: Contract and crows-nest slice

Phase D is the hand-off: class-t4 publishes everything crows-nest needs in the ops contract, and crows-nest builds the minimum it must provide for M2. D2 to D4 are tracked as issues in crows-nest and mirrored here as dependencies.

**D1. Ops contract for M2** · S · Claude · A1 to B4

- **Deliverables:** updated `docs/ops-contract.md`.
- **Details:** adds the on-host layout, `class-t4-deploy` commands and exit codes, release asset names (`class-t4-<version>-aarch64.tar.gz`, `.minisig`, `SHA256SUMS`, SBOM), cert and key paths with owners and modes, "cert change means `class-t4-deploy restart`", the firewall table, and host prerequisites (`minisign`, `curl`, `tar`, `flock`). A contract change bumps the class-t4 minor version.
- **Validate:** your review; crows-nest mirrors it into `host_vars/epiphany.yml`.

**D2. Firewall and prerequisites on epiphany** · external · crows-nest · D1

- **Details:** `base` role applies the nftables rules from the firewall table and ensures the host prerequisites.
- **Validate:** H-02, H-03.

**D3. Internal CA and `tls_cert` for epiphany** · external · crows-nest · D1, D8

- **Details:** the internal CA lives on tranquility; `tls_cert` issues a 30-day cert with the D8 SANs, writes it to the contract paths with the contract owners and modes, then runs `class-t4-deploy restart` if a release is active. On the first deploy the playbook runs `install`, then `tls_cert`, then `activate`: `install` creates the `class-t4` group the key needs, and the gate needs the cert.
- **Validate:** H-01, H-06.

**D4. Deploy playbook** · external · crows-nest · D1

- **Deliverables:** `playbooks/deploy-class-t4.yml` in crows-nest.
- **Details:**
  1. Input: `class_t4_version`, or a local artifact path for the drill (D7).
  2. On tranquility: download the release assets into a staging directory, check `SHA256SUMS`, verify the minisign signature with the class-t4 public key from `host_vars`.
  3. Copy the tarball and signature to `/var/cache/class-t4-deploy/` on epiphany.
  4. Run `class-t4-deploy install`, then `tls_cert` for the host if no cert exists yet, then `class-t4-deploy activate`; fail the play on any non-zero exit and print the deploy tool's exit code and meaning.
- **Validate:** E1 is its first real run.

### E: First deploy and drills on epiphany

Phase E is yours: it proves on the real host what phase B proved with stubs. The release sequence is `v0.2.0-rc.1` (first deploy), `0.2.0-drill.1` (must fail), `v0.2.0-rc.2` (manual rollback test), then `v0.2.0`.

**E1. Bootstrap and first deploy** · S · **you** · A4, C2, D2 to D4, M1 release procedure for `v0.2.0-rc.1`

- **Details:**
  1. Copy `keys/class-t4.pub` to `/etc/class-t4/minisign.pub` on epiphany; check its key ID matches the repo copy.
  2. On epiphany, verify the rc tarball by hand with `minisign -Vm`, then copy its `share/deploy/class-t4-deploy` to `/usr/local/sbin/`. This is the only manual install of the tool; later releases update it.
  3. Confirm the internal CA can issue; the playbook writes the first cert between `install` and `activate`.
  4. Run the deploy playbook for `v0.2.0-rc.1`.
- **Validate:** H-01 to H-03, H-07, H-08.

**E2. Rollback drills** · S · **you** · E1, D7

- **Details:**
  1. Build `0.2.0-drill.1` from `drill/fail-health`, where `/health` returns 503. Sign it with your key and the trusted comment `class-t4 v0.2.0-drill.1`; stage it on tranquility only.
  2. Deploy it with the playbook's local-artifact option. Expect exit 10 and `rc.1` serving.
  3. Deploy `v0.2.0-rc.2` (a normal rc), run `class-t4-deploy rollback`, confirm `rc.1` serves, then activate `rc.2` again.
- **Validate:** H-04, H-05.

**E3. Cert rotation drill** · S · **you** · E1

- **Details:** force a renewal with crows-nest's `certs.yml`; check from lovelace that the served certificate's serial changed.
- **Validate:** H-06.

**E4. Baselines** · S · **you** · E1

- **Details:** record idle memory (`systemctl show -p MemoryCurrent class-t4`), restart-to-healthy time and deploy duration from the deploy tool's log, the `systemd-analyze security` score on the host, and the tarball size.
- **Validate:** values added to the project plan change log.

### F: Docs and release

Phase F writes down what phase E proved and ships `v0.2.0` through it.

**F1. Runbooks** · S · Claude drafts from your E-phase notes, you review · E1 to E3

- **Deliverables:** `docs/runbooks/` `first-deploy.md`, `upgrade.md`, `rollback.md`, `cert-rotation.md` (the last points to crows-nest for issuance and covers only the class-t4 side).
- **Details:** each runbook lists the exact commands, the expected output, the deploy tool's exit codes, and what to do on each failure code.
- **Validate:** your review; the `v0.2.0` deploy in F3 follows `upgrade.md` word for word.

**F2. ADRs and changelog** · S · Claude · E2

- **Deliverables:** `docs/adr/0006-deploy-model.md` (tranquility-driven, signed, gated), `0007-service-identity.md` (static user, state directory), `0008-sbom-and-cve-scan.md`; a `v0.2.0` section in `CHANGELOG.md`.
- **Validate:** your PR review.

**F3. Release and deploy `v0.2.0`** · S · **you** · F1, F2

- **Details:** the M1 release procedure, then `upgrade.md` with the deploy playbook.
- **Validate:** H-09.

## Test plan

32 tests prove M2: 16 deploy-tool tests, 5 build and config checks, 2 workflow tests run in CI, and 9 host tests you run on epiphany. Every failure path the deploy tool has is exercised off-host before it is trusted on-host.

### Deploy tool tests (`tests/deploy/run.sh`, CI lint job)

| ID | Test | Proves | Task |
| --- | --- | --- | --- |
| T-01 | Tampered tarball: exit 3, nothing written | Signature is enforced | B1 |
| T-02 | Valid signature from a different key: exit 3 | Only your key is trusted | B1 |
| T-03 | File name, trusted comment and top-level directory disagree on version: exit 3 | A signed artifact cannot be relabeled | B1 |
| T-04 | Archive with `..`, an absolute path or an escaping symlink: exit 3, nothing unpacked | No path traversal | B1 |
| T-05 | Same version installed twice: exit 0, no change | Install is idempotent | B1 |
| T-06 | Installed release has no write bits | Releases are immutable | B1 |
| T-07 | Three good polls: `current` is new, `previous` is old, tool refreshed, exit 0 | Happy path | B2 |
| T-08 | Probe returns the old version: rollback, exit 10 | A stale process cannot pass the gate | B2 |
| T-09 | No answer for 30 s, or `NRestarts` changes: rollback, exit 10, `previous` restored | Crash loops and hangs are caught | B2 |
| T-10 | `rollback` with no previous release: exit 4 | Safe failure | B3 |
| T-11 | Rollback gate also fails: exit 11, `crit` journal entry | Double failure is loud | B3 |
| T-12 | Second run while the lock is held: exit 5 | No concurrent deploys | B1, B4 |
| T-13 | `prune` with five releases keeps three, never `current` or `previous` | Retention is safe | B4 |
| T-14 | First install creates the user, installs and enables the unit; an existing config is untouched without `--update-config` | Bootstrap and config safety | B1 |
| T-15 | Release with a changed unit: new unit installed and reloaded; rollback restores the old unit | Unit changes roll back too | B2, B3 |
| T-16 | `restart` whose gate fails: exit 10, no link changes | Cert rotations are gated | B4 |

### Build and config checks (CI lint job)

| ID | Test | Proves | Task |
| --- | --- | --- | --- |
| S-01 | `systemd-analyze verify` on the unit | Unit is valid | A1 |
| S-02 | Offline `systemd-analyze security` exposure 2.5 or lower | Hardening holds | A1 |
| S-03 | `systemd-sysusers --root=<tmp>` creates `class-t4` with `/usr/sbin/nologin` | User definition works | A2 |
| S-04 | Tarball v2 listing: new files present, modes correct | Package carries the deploy assets | A4 |
| U-01 | `deploy/epiphany/class-t4.json` loads with the server's config loader | Shipped config is valid | A3 |

### Workflow tests (GitHub Actions)

| ID | Test | Proves | Task |
| --- | --- | --- | --- |
| W-01 | Image smoke test checks `minisign`, `shellcheck`, `systemd-analyze` | Image has the new tools | C1 |
| W-02 | CVE scan on a fixture SBOM with a known-vulnerable OpenSSL fails; dry run opens no issue | The scan can actually fail | C3 |

### Host tests (you, on epiphany and the LAN)

| ID | Test | Pass when | Task |
| --- | --- | --- | --- |
| H-01 | From a LAN client with the internal CA root: `curl --cacert root.crt https://<epiphany address>:8443/health` | 200, version `0.2.0-rc.1`, no TLS errors | E1 |
| H-02 | `curl http://epiphany:9464/metrics` from tranquility, then from lovelace | 200 from tranquility; refused or timed out from lovelace | D2, E1 |
| H-03 | Port scan of epiphany from lovelace | Only 8443 open | D2 |
| H-04 | Drill artifact through the playbook | Playbook reports exit 10; `rc.1` serving; journal shows the gate failure | E2 |
| H-05 | `rollback` after deploying `rc.2` | `rc.1` serving; `rc.2` re-activates cleanly | E2 |
| H-06 | Forced cert renewal | New serial served; journal shows a passed gate | E3 |
| H-07 | Reboot epiphany | Service active and healthy within 60 s of boot | E1 |
| H-08 | `systemctl show -p MemoryHigh,MemoryMax class-t4`; `sudo -u class-t4 touch /opt/class-t4/current/x` | Limits match the spec; write is denied | E1 |
| H-09 | `class-t4-deploy status` after F3 | `current` is `0.2.0`, `previous` is `0.2.0-rc.2` | F3 |

## Acceptance and sign-off

M2 is done when `v0.2.0` is serving on epiphany through the documented path and every box below is ticked; you are the approver.

- [ ] All 23 tasks closed (D2 to D4 closed in crows-nest)
- [ ] All 32 tests pass where they are meant to run
- [ ] `docs/ops-contract.md` updated and mirrored in crows-nest's `host_vars/epiphany.yml`
- [ ] Runbooks, ADRs 0006 to 0008 and the `v0.2.0` changelog merged
- [ ] `v0.2.0` published with tarball, `.minisig`, `SHA256SUMS` and SBOM, and deployed by `upgrade.md`
- [ ] Baselines from E4 recorded in the project plan

## Risks

The biggest schedule risk is crows-nest: M2 cannot reach epiphany until its firewall, CA and deploy playbook exist, and whether that work has started is an open item.

| Risk | Trigger | Response |
| --- | --- | --- |
| crows-nest slice lags class-t4 | Phases A to C done, D2 to D4 open | Start D2 to D4 as soon as D1 (decision) is confirmed; in the worst case, hand-run the playbook's steps from the runbook once, then automate |
| Gate passes a bad build | A release that starts but misbehaves | Gate checks version on both listeners and restart count; M4 adds functional checks to `/health` |
| Hardening breaks Drogon at runtime (for example `MemoryDenyWriteExecute` or the syscall filter) | E1 service fails with `SIGSYS` or permission errors | Relax the one setting, record it in ADR 0007, keep the exposure target |
| No cert at first start | First activate without a cert | The playbook issues the cert between `install` and `activate` |
| Static OpenSSL CVE between scans | Advisory published mid-week | Weekly scan plus `workflow_dispatch` on advisories; patch release path is the normal deploy |
| Root on epiphany can still edit releases | Manual tampering | Out of scope for M2; read-only modes guard against mistakes, not root |

## Open items

- [ ] Confirm D1 to D8, or change any of them
- [ ] D8 decided: IP SAN only for now
- [ ] LAN subnet for the 8443 firewall rule, and tranquility's address for the 9464 and 22 rules
- [ ] crows-nest order before phase E: M0, M1 (`base`), then M3 core (internal CA, `tls_cert`, deploy playbook T23, ports and prerequisites T24)
- [ ] Decided: SSH to epiphany from tranquility only; the console is break-glass

## Change log

| Date | Change |
| --- | --- |
| 2026-11-12 | Created from the project plan, the ops contract on the M1 branch and the crows-nest reference |
| 2026-11-12 | Cross-repo review applied: IP-only SAN, internal CA without a daemon, first deploy runs install, cert, activate; SSH from tranquility only; no output chain; prune deferred; crows-nest tasks referenced by ID |
