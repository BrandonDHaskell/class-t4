# class-t4 M5: Operable and Restorable

Nov 12, 2026 · @Brandon

## Overview

M5 ships `v1.0.0`: nothing new for the household, but every failure of epiphany now pages you by email, every metric and log lands on tranquility, the 4GB budget is proven under load, and a full rebuild from bare hardware has been timed. It closes the last two success criteria of the [project plan](https://claude.ai/artifact/Eoy2RvQxbejN5U8tmEtY6J): failures stay visible and recoverable, and restore is tested, not just configured.

**Exit criteria**

- Every alert in the catalog has passing rule tests, and the five critical paths have fired for real and reached your inbox.
- Grafana shows service, sync and host dashboards; Loki holds class-t4's logs with no secrets in them.
- Load tests meet the performance budgets, and memory stays inside the systemd limits.
- A disaster-recovery drill on a spare NVMe rebuilt epiphany from bare hardware within the RTO, losing no more than the RPO.
- The operational readiness review (ORR) is signed off, and `/api/v1` is declared stable.

**Entry criteria**

- [ ] M4 done: `v0.6.0` through its 72-hour soak
- [ ] Decisions D1 to D10 confirmed
- [ ] crows-nest has Prometheus, Alertmanager, Grafana, Loki and the restic jobs installed (their M5 configuration is phase B)
- [ ] A spare NVMe drive for the recovery drill

**What `v1.0.0` commits to:** `/api/v1` changes only additively from here; the ops contract changes only with a minor version bump; every alert links to a runbook.

## Observability topology

epiphany only exposes signals; tranquility collects, stores, judges and alerts; and one external heartbeat watches tranquility itself.

&#91;embedded content: observability topology · epiphany, tranquility and you\]

Arrows show which way data flows; Prometheus and restic pull from epiphany, so epiphany holds no credentials for tranquility.

## Decisions

Ten decisions shape M5. D6 and D7 matter most: they decide how risky the recovery drill is and how much data a real failure can lose.

| # | Decision | Recommended | Alternatives |
| --- | --- | --- | --- |
| D1 | Alert severities and delivery | Two severities by email (crows-nest's channel): `critical` sends at once and repeats every 4 h; `warning` groups for 30 min and repeats every 24 h | One severity; phone push via ntfy |
| D2 | Alert catalog | The 8 app alerts in the spec, each with a runbook link; host, cert and backup alerts belong to crows-nest | Fewer, coarser alerts |
| D3 | Service level objectives | Availability 99.5% per 30 days (blackbox probe from tranquility); freshness: 95% of sync runs succeed within 2 intervals | No formal SLOs |
| D4 | Load test tool | k6, pinned by image digest, run from lovelace; scripts in `tests/load/` | `oha` or `wrk`; a custom C++ client |
| D5 | Performance budgets | Month view p95 at most 150 ms at 20 requests/s; zero 5xx; RSS at most 512 MB during the soak | Measure first, set later |
| D6 | Recovery drill method | Rebuild on a spare NVMe from runbooks only; the live NVMe stays untouched as the fallback | Wipe the live drive; restore onto a second Pi |
| D7 | Recovery targets | RTO 2 hours from bare drive to healthy service; RPO 24 hours, matching crows-nest's nightly pull (decision 47) | RPO 1 hour with hourly pulls |
| D8 | Log contract | JSON fields `ts`, `level`, `msg`, `component`, optional `source_id`; Loki labels limited to `host`, `unit`, `level` and `job` (crows-nest decision 42); 30-day retention | Free-form logs |
| D9 | Latency metric | Add `class_t4_http_request_duration_seconds` histogram by route pattern | Infer latency from blackbox probes only |
| D10 | `v1.0.0` gate | A written ORR signed by you, plus the `/api/v1` stability statement | Tag when tests pass |

**Reasoning**

- **D6:** a drill that wipes the only working drive turns a test into an outage if any runbook step is wrong. A spare drive keeps the drill honest (nothing but runbooks and backups) while the real system stays one swap away.
- **D7:** the database is a cache of upstream calendars plus a handful of source rows, so losing a day costs at most re-adding a source or one re-consent. Nightly pulls match crows-nest decision 47 and keep the alert thresholds tolerant of tranquility's Wi-Fi.
- **D10:** the ORR is the single page that proves ownership, alarms, runbooks, dependencies and recovery are all in place before calling the service 1.0.

## Specifications

These assume the recommended decisions. Alert expressions use the metric names defined in M1 to M4; anything crows-nest exports (restic, restore drill) uses the names crows-nest chooses in task B6.

### Service level objectives

| SLO | Target | Measured by |
| --- | --- | --- |
| Availability | 99.5% of 30 days (about 3.6 hours of allowed downtime) | `probe_success` of the HTTPS `/health` probe from tranquility |
| Freshness | 95% of sync runs succeed within 2 intervals of being due | `class_t4_calendar_sync_runs_total` and `class_t4_calendar_sync_last_success_timestamp_seconds` |
| API latency | p95 of month-view requests at most 150 ms | `class_t4_http_request_duration_seconds` |

### Alert catalog

| Alert | Fires when | For | Severity | Runbook |
| --- | --- | --- | --- | --- |
| `ClassT4Down` | `up{job="class-t4"} == 0` | 2m | critical | `service-down.md` |
| `ClassT4HttpsHealthFailing` | HTTPS `/health` probe fails | 5m | critical | `service-down.md` |
| `ClassT4RestartLoop` | more than 3 restarts in 1 hour (`process_start_time_seconds` changes) | 0m | warning | `restart-loop.md` |
| `ClassT4High5xx` | 5xx above 5% of requests | 10m | warning | `service-down.md` |
| `ClassT4MemoryNearLimit` | RSS above 700 MB (`MemoryHigh` is 768 MB) | 15m | warning | `memory.md` |
| `ClassT4SnapshotStale` | newest hourly snapshot older than 3 hours | 0m | warning | `backups.md` |
| `ClassT4SourceNeedsReconsent` | any source in `needs_reconsent` | 10m | warning | `reconsent.md` |
| `ClassT4SourceStale` | an active source without a successful sync for 2 hours | 15m | warning | `sync-troubleshooting.md` |

crows-nest owns the host and platform alerts and their names: `HostDown`, `HostUnreachable`, `DiskFillingSoon`, `CertExpiringSoon`, `BackupStale`, `RestoreDrillFailed` and the log-pipeline alerts. class-t4 ships the eight app alerts above as `ops/alerts/class-t4.yml`, with rule tests and its two dashboards, in each release; crows-nest loads that bundle at the pinned version. crows-nest's always-firing `Watchdog` feeds the external heartbeat; if it stops, healthchecks.io emails you. The five critical paths are `ClassT4Down`, `ClassT4HttpsHealthFailing`, `HostDown`, `CertExpiringSoon` at its critical threshold, and the heartbeat.

### Dashboards (provisioned in crows-nest)

| Dashboard | Panels |
| --- | --- |
| class-t4 service | Up and version, requests by route and code, p95 latency, RSS against `MemoryHigh` and `MemoryMax`, restarts, days to cert expiry, SLO burn for the month |
| Sync | Status per source, age of last success, run results, duration p95, occurrences per source |
| Backups | Snapshot age, restic pull age, restore drill result and duration |
| Hosts | node\_exporter for epiphany and tranquility: CPU, temperature, memory, disk, network |

### Log contract

| Field | Rule |
| --- | --- |
| `ts` | UTC, RFC 3339 with milliseconds |
| `level` | `trace`, `debug`, `info`, `warn`, `error` |
| `msg` | Short, constant text (variables go in fields) |
| `component` | `server`, `http`, `db`, `sync`, `outbound`, `secrets`, `cli` |
| `source_id` | Optional, numeric |

Never logged: tokens, passwords, keys, `Authorization` headers, full feed URLs, and event content (titles, locations). Loki labels are only `host`, `unit`, `level` and `job`; retention is 30 days.

### Performance budgets and load scenarios

Data for the tests: a `loadtest` owner with 5,000 synthetic occurrences, created with an admin-only `class-t4 dev seed` command and removed afterward.

| Scenario | Load | Duration | Pass when |
| --- | --- | --- | --- |
| L-1 Steady | 20 requests/s: 80% month view, 20% `/sources` | 30 min | p95 at most 150 ms; zero 5xx |
| L-2 Ramp | 5 to 100 requests/s | 20 min | The rate where p95 crosses 150 ms is recorded, and is at least 40 requests/s |
| L-3 Soak | 5 requests/s plus a forced full resync of every source every 30 min | 4 h | RSS at most 512 MB with no upward trend; every sync succeeds; no thermal throttling |

### Recovery targets

| Target | Value | Measured from, to |
| --- | --- | --- |
| RTO | 2 hours | Blank spare NVMe inserted, to gate passed with every source `active` or listed for reconsent |
| RPO | 24 hours | Time of the restored snapshot, to the simulated failure time |

Off-host copies needed for a rebuild, all in your password manager: the class-t4 token key and ID, `oauth-clients.json`, the `api-token`, and the minisign secret key password (the key file itself lives on removable encrypted media).

## Execution plan

19 tasks in 5 phases: about 11 sessions in class-t4 plus about 6 in crows-nest. Unlike M1 to M4, most of M5's weight sits in crows-nest and in drills you run, so phase B is the critical path.

| Phase | Tasks | Sessions | Your touchpoints | Exits with |
| --- | --- | --- | --- | --- |
| A class-t4 observability gaps | A1 to A3 | 3 | PR review | Latency histogram, log contract, contract v1.0 |
| B crows-nest monitoring, logs, backups | B1 to B6 | \~6 in crows-nest | crows-nest work | Scrapes, alerts, dashboards, logs, nightly pulls, restore-drill job |
| C Performance | C1 to C3 | 3 | Run C2 | Budgets met or tuned |
| D Recovery and alert drills | D1 to D4 | 3 plus a half-day drill | D2, D4 | RTO and RPO measured; critical alerts proven live |
| E Release readiness | E1 to E3 | 2 | E1 sign-off, E3 | ORR signed, `v1.0.0` running |

- **Critical path:** B1 → B2 → B3 → D4 → E1 → E3.
- **Second path:** A1 → C1 → C2 → (C3 if needed) → E1.
- **Recovery path:** B6 → D1 → D2 → D3 → E1.
- **Workflow:** unchanged. Load tests run against epiphany during a quiet window, never against tranquility.

Task format below: **ID. Name** · size · owner · depends on, then details and validation.

## Tasks

### A: class-t4 observability gaps

Phase A closes the few gaps between what class-t4 already exports and what the alert catalog and dashboards need.

**A1. Metrics completion** · S · Claude · M4

- **Details:** add `class_t4_http_request_duration_seconds` (histogram by `listener` and route pattern; buckets 5, 10, 25, 50, 100, 150, 250, 500, 1000 and 2500 ms). Confirm `process_resident_memory_bytes` and `process_start_time_seconds` exist (proposed in M1; add them if the branch skipped them). Unmatched paths stay `route="other"` so cardinality is bounded.
- **Validate:** U-01, I-01, I-02.

**A2. Log contract enforcement** · M · Claude · M4

- **Details:** every log call carries `component`; sync logs carry `source_id`. `secret_string` and the token-store types cannot be passed to the logger (compile-time). When `JOURNAL_STREAM` is set, each line starts with its sd-daemon priority prefix (`<3>` to `<7>`), so journald records the level and crows-nest's `level` label and debug filter apply; without it the output stays pure JSON. The integration suite plants canary values (`SECRET-CANARY-<n>`) as tokens, passwords, feed URLs and event titles, and a post-suite check scans every captured log line for them.
- **Validate:** I-03, I-04.

**A3. Ops contract v1.0** · S · Claude · A1, A2

- **Details:** the contract gains the alert-relevant metrics with their meaning, the log schema, the SLO definitions, the off-host copies a rebuild needs, and the `v1.0.0` stability statement.
- **Validate:** your review; crows-nest mirrors it in phase B; the contract metric test stays green.

### B: crows-nest monitoring, logs and backups

Phase B is crows-nest work, tracked as issues there and mirrored here. Most of it is already specified in crows-nest M2 to M5; these tasks list only what class-t4 adds, and where the two differ the crows-nest doc wins. Everything is configuration as code, so every piece has an offline test in crows-nest's CI before it touches tranquility.

**B1. Scrape jobs and probes** · external · crows-nest · A3

- **Details:** `class-t4` job on `epiphany:9464` every 30 s; node\_exporter on both hosts; blackbox HTTPS probe of `https://<epiphany address>:8443/health` trusting the internal CA root (also yields cert expiry); blackbox HTTP probe of `epiphany:9464/health`.
- **Validate:** `promtool check config` in CI; P-01.

**B2. Alert rules** · external · crows-nest · B1

- **Deliverables:** in class-t4, `ops/alerts/class-t4.yml` and `ops/alerts/tests/class-t4_test.yml`; in crows-nest, the step that loads the bundle from the pinned release into `monitoring/rules/`.
- **Details:** every app alert in the catalog, with `summary`, `description` and a `runbook_url` pointing at the class-t4 runbook on GitHub.
- **Validate:** `promtool test rules` with a firing and a non-firing case per alert (R-01 to R-08), run in class-t4 CI and again in crows-nest CI on the loaded bundle.

**B3. Alertmanager routing** · external · crows-nest · B2

- **Details:** routes per D1; `HostDown` inhibits `ClassT4Down` and `ClassT4HttpsHealthFailing` so a dead host sends one email, not three; `Watchdog` routes to the heartbeat.
- **Validate:** `amtool check-config` and `amtool config routes test` in CI (R-15, R-16).

**B4. Dashboards** · external · crows-nest · B1, A1

- **Details:** the service and sync dashboards ship in class-t4's bundle (`ops/dashboards/`); the backups and hosts dashboards are crows-nest's own. All four are provisioned from `monitoring/dashboards/`.
- **Validate:** G-01.

**B5. Log pipeline** · external · crows-nest · A2

- **Details:** `systemd-journal-upload` on epiphany to `systemd-journal-remote` on tranquility over mutual TLS with internal CA certs; Alloy labels lines by journal priority (class-t4 sets it through the priority prefix from A2), keeps only the `host`, `unit`, `level` and `job` labels, and writes to Loki with 30-day retention.
- **Validate:** G-02, G-03.

**B6. Nightly pulls and the restore-drill job** · external · crows-nest · M3 D5

- **Details:** nightly restic pull (crows-nest decision 47); retention of 7 daily, 4 weekly and 6 monthly snapshots (decision 13); the weekly `restore-drill.yml` run from the crows-nest reference; both export last-success time and result through node\_exporter's textfile collector for the backup alerts.
- **Validate:** B-01 to B-03.

### C: Performance

Phase C proves the 4GB budget with numbers instead of assumptions, using lovelace as the load generator as the project description intends.

**C1. Load test kit and seed data** · M · Claude · A1

- **Deliverables:** `tests/load/` (k6 scripts for L-1 to L-3, image digest pinned in `tests/load/IMAGE`), `scripts/load/run.sh <scenario>`, and `class-t4 dev seed` and `dev unseed`.
- **Details:** `run.sh` runs k6 in Docker on lovelace against epiphany's HTTPS port, trusting the internal CA root, sending the API token, and saves the k6 summary as JSON. `dev seed --owner loadtest --occurrences 5000` creates a paused synthetic source (the scheduler never syncs it) with deterministic occurrences; `dev unseed` removes it. Both refuse any owner name other than `loadtest`.
- **Validate:** L-0, a short dry run of every scenario against a local `host-debug` server.

**C2. Run the scenarios** · S · **you** run, Claude analyzes · C1, B1, B4

- **Details:** in a quiet window, seed, run L-1, L-2 and L-3, unseed. Export the matching Grafana panels (p95, RSS, CPU, temperature) for each run. Claude writes `docs/perf/v1.0.md` from the k6 summaries and the panels.
- **Validate:** L-1 to L-3 against the budgets.

**C3. Tune if a budget fails** · M, only if needed · Claude · C2

- **Details:** candidates in order of cost: IO thread count, SQLite `cache_size`, a short-lived in-memory cache of the month view keyed by the `ETag`, and the systemd memory limits. Each change gets its own PR and a rerun of the failed scenario.
- **Validate:** the failed scenario passes on rerun; the change is recorded in `docs/perf/v1.0.md`.

### D: Disaster recovery and alert drills

Phase D tests the two things monitoring and backups exist for: that you hear about failures, and that you can recover from the worst one using only runbooks, backups and your password manager.

**D1. Disaster-recovery runbook** · M · Claude drafts, you review · B6, A3

- **Deliverables:** `docs/runbooks/disaster-recovery.md`.
- **Details:** one ordered procedure that links the existing runbooks rather than copying them: Pi 5 host build, kernel, crows-nest's epiphany playbook (base, firewall, certs, agents), class-t4 bootstrap (minisign key, deploy tool), credentials from the password manager (`token-key`, `oauth-clients.json`), deploy of the current release, `class-t4-deploy restore` from the newest restic snapshot, verification. Each step has a checkpoint where you note the time. It starts with a check that every off-host copy listed in the spec actually exists.
- **Validate:** a read-through finds no step that needs anything outside the runbooks, backups or password manager.

**D2. Recovery drill on the spare NVMe** · half a day · **you** · D1

- **Details:**
  1. Note the latest restic snapshot time, then record the simulated failure time and power epiphany off.
  2. While it is off, confirm `HostDown` arrives as a single email (the inhibition from B3).
  3. Swap in the blank spare NVMe and follow `disaster-recovery.md` exactly, noting each checkpoint time and every deviation.
  4. Afterward, decide whether to keep the rebuilt drive or swap the original back.
- **Validate:** X-01 to X-04, F-03.

**D3. Runbook corrections** · S · Claude · D2

- **Details:** fold every deviation from the drill log into the runbook; open an issue for each manual step worth automating later.
- **Validate:** every deviation is either fixed in the runbook or has an issue.

**D4. Live alert drills** · S · **you** · B3

- **Details:** fire each remaining critical path for real and time the email: stop `class-t4` (`ClassT4Down`); temporarily drop port 8443 in nftables (`ClassT4HttpsHealthFailing`); issue a 48-hour cert through `tls_cert` (`CertExpiringSoon`, critical), then renew normally; stop Alertmanager on tranquility (heartbeat email from healthchecks.io). Restore each condition and confirm the resolved email.
- **Validate:** F-01, F-02, F-04, F-05.

### E: Release readiness and `v1.0.0`

Phase E gathers the evidence from A to D into one review and ships `v1.0.0` on it.

**E1. Operational readiness review** · M · Claude drafts, **you** sign · C2, D3, D4

- **Deliverables:** `docs/orr-v1.0.md`.
- **Details:** one row per question, each answered with a link to evidence:
  - Ownership and architecture: who runs it, where the design and contract live.
  - Dependencies: crows-nest, tranquility (Wi-Fi), the four providers, and what each failure does to the household view.
  - SLOs, dashboards and the alert-to-runbook table (the 8 app alerts plus crows-nest's).
  - Recovery: measured RTO and RPO from D2.
  - Release and rollback: the M2 path and its drills.
  - Security: `systemd-analyze security` score, latest CVE scan, secrets inventory and where each copy lives, egress allowlist, firewall, tailnet policy, kernel age, and the accepted risk that tranquility's Ansible key is root on epiphany.
  - Capacity: results from `docs/perf/v1.0.md`.
  - Accepted risks, each with a reason.
- **Validate:** every row links to evidence; you sign it.

**E2. Docs and API freeze** · S · Claude · E1

- **Deliverables:** `README.md` (purpose, architecture, development quickstart, runbook index); `openapi.yaml` at `info.version: 1.0.0`; `CHANGELOG.md` `1.0.0` section; `docs/adr/0018-v1-stability.md` (API additive only; contract changes need a minor version).
- **Validate:** D-01 (every runbook named in the contract's alert table exists in the repo).

**E3. Release and deploy `v1.0.0`** · S · **you** · E1, E2

- **Details:** the M1 release procedure, `upgrade.md`, then 24 hours watching the dashboards. Afterward, mark M5 and every project success criterion done in the project plan.
- **Validate:** H-01.

## Test plan

37 tests prove M5. Most of M5 is configuration, so the core of the plan is offline rule and routing tests in CI, backed by a small number of live drills that prove the email actually arrives.

### class-t4 tests (CI)

| ID | Test | Task |
| --- | --- | --- |
| U-01 | Histogram buckets match the spec; unmatched paths get `route="other"` | A1 |
| I-01 | After requests, `/metrics` shows the histogram with every bucket | A1 |
| I-02 | 1,000 requests to random paths add no route label besides `other` | A1 |
| I-03 | No captured log line from the whole integration suite contains a `SECRET-CANARY` value | A2 |
| I-04 | Every captured log line matches the log schema (fields, `component` values) | A2 |
| D-01 | Every runbook named in the contract's alert table exists in the repo | E2 |

### Alerting tests (crows-nest CI)

| ID | Test | Task |
| --- | --- | --- |
| R-01 to R-08 | `promtool test rules`: for each of the 8 app alerts, one series that fires after its `for` duration and one that stays quiet | B2 |
| R-15 | `amtool config routes test`: critical alerts reach the immediate route, warnings the grouped route, `Watchdog` the heartbeat | B3 |
| R-16 | `amtool check-config` and `promtool check config` pass | B1, B3 |

### Pipeline checks (you, on tranquility)

| ID | Test | Pass when | Task |
| --- | --- | --- | --- |
| P-01 | Prometheus targets page | `class-t4`, both node\_exporters and both probes are up | B1 |
| G-01 | Open the four dashboards | Every panel has data | B4 |
| G-02 | Loki query `{unit="class-t4.service"} \| json \| level="error"` | Works; streams carry only `host`, `unit`, `level` and `job` labels | B5 |
| G-03 | Write a log line on epiphany, then reboot tranquility | Line visible in Loki within 60 s; upload resumes after the reboot | B5 |
| B-01 | Restic snapshots over 2 nights | One new snapshot per night; last-success metric under 26 hours old | B6 |
| B-02 | Weekly restore drill, then a forced failure with a corrupt test snapshot | Success recorded; the forced failure raises `RestoreDrillFailed` | B6 |
| B-03 | `restic forget --dry-run` | Keeps 7 daily, 4 weekly and 6 monthly | B6 |

### Load tests (lovelace against epiphany)

| ID | Test | Pass when | Task |
| --- | --- | --- | --- |
| L-0 | Every scenario for 1 minute against a local `host-debug` server | Scripts run cleanly | C1 |
| L-1 | Steady, 20 requests/s for 30 min | p95 at most 150 ms; zero 5xx | C2 |
| L-2 | Ramp, 5 to 100 requests/s | Knee recorded, at or above 40 requests/s | C2 |
| L-3 | Soak, 4 h with forced resyncs | RSS at most 512 MB, no upward trend, every sync succeeds, no throttling | C2 |

### Recovery drill (you)

| ID | Test | Pass when | Task |
| --- | --- | --- | --- |
| X-01 | Recovery time | Blank drive to healthy service in 2 hours or less | D2 |
| X-02 | Data loss | Restored snapshot no more than 24 hours older than the failure time | D2 |
| X-03 | Data after restore | Canary opens; every source `active` or listed for reconsent; occurrences return through the API | D2 |
| X-04 | Fallback | The original NVMe still boots and serves, if swapped back | D2 |

### Live alert drills (you)

| ID | Test | Pass when | Task |
| --- | --- | --- | --- |
| F-01 | Stop `class-t4` | `ClassT4Down` email within 5 min; resolved email after restart | D4 |
| F-02 | Drop port 8443 | `ClassT4HttpsHealthFailing` email within 8 min | D4 |
| F-03 | Power epiphany off (during D2) | Exactly one email: `HostDown` | D2 |
| F-04 | 48-hour cert | `CertExpiringSoon` (critical) email; clears after renewal | D4 |
| F-05 | Stop Alertmanager on tranquility | Heartbeat email from healthchecks.io | D4 |

### Release check (you)

| ID | Test | Pass when | Task |
| --- | --- | --- | --- |
| H-01 | `v1.0.0` deployed, then 24 hours | No unexpected alerts; the SLO panel shows the month's availability | E3 |

## Acceptance and sign-off

M5, and the project's first version, is done when you sign the ORR and `v1.0.0` passes its 24-hour watch.

- [ ] All 19 tasks closed (B1 to B6 closed in crows-nest)
- [ ] All 37 tests pass where they are meant to run
- [ ] RTO and RPO measured and within target, recorded in the ORR
- [ ] `docs/perf/v1.0.md`, `disaster-recovery.md`, ADR 0018, README and the `1.0.0` changelog merged
- [ ] ORR signed
- [ ] `v1.0.0` published and deployed; project plan updated with every success criterion done

## Risks

The weakest link is tranquility itself: it is on Wi-Fi and it hosts every piece of monitoring, so the heartbeat (F-05) is the alert that protects all the others.

| Risk | Trigger | Response |
| --- | --- | --- |
| tranquility drops off Wi-Fi | Scrapes and pulls stop | Heartbeat email; `BackupStale` once it is back; consider wired Ethernet |
| Alert noise trains you to ignore email | More than a few warnings a week | Tune thresholds after the first month; only four alerts are critical |
| A runbook step needs something not backed up | D2 stalls | That is the purpose of the drill: D3 fixes it before `v1.0.0` |
| Restored tokens were rotated after the snapshot | Microsoft or Google sources fail after a restore | Expected within the 24-hour RPO; reconsent through the runbook |
| Load tests disturb the household | Slow calendar during a test | Quiet window; seeded data is paused and removed afterward |
| Pi 5 throttles under sustained load | Temperature panel shows throttling during L-3 | Record it; add cooling before `v1.0.0` if it does |
| crows-nest work slips | Phase B incomplete | M5 cannot close without it; it is the critical path and should start right after M4's `v0.4.0` |

## Open items

- [ ] D1 to D10 confirmed on 2026-11-12 (D7: nightly pulls, RPO 24 hours); D6 still needs the spare NVMe
- [ ] Do you have a spare NVMe drive for the drill, or does one need ordering?
- [ ] Copy `oauth-clients.json` into your password manager (the M4 docs did not call for an off-host copy)
- [ ] Should tranquility move to wired Ethernet before `v1.0.0`?
- [ ] Alert email address, and whether critical alerts should also go to ntfy on your phone

## Change log

| Date | Change |
| --- | --- |
| 2026-11-12 | Created from the project plan, the crows-nest reference and the M2 to M4 docs |
| 2026-11-12 | Cross-repo review applied: alert catalog cut to 8 app alerts shipped as a bundle, host and platform alerts owned by crows-nest; nightly pulls and RPO 24 hours; journal priority prefix; four Loki labels; internal CA; M4 ends at v0.6.0 |
