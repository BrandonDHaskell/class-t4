# class-t4 M3: Persistence and Secrets

Nov 12, 2026 · @Brandon

## Overview

M3 ships `v0.3.0`: class-t4 gains a SQLite database with module-owned migrations, an encrypted token store keyed from a systemd credential, a locked-down outbound HTTPS client, and backups that crows-nest can pull and you can restore. It still serves no calendar data; M4 builds on all four pieces. Roadmap context: [class-t4: Project Plan](https://claude.ai/artifact/Eoy2RvQxbejN5U8tmEtY6J).

**Exit criteria**

- `v0.3.0` runs on epiphany with the database under `/var/lib/class-t4/` and the token key loaded from a systemd credential.
- A deploy with a pending migration snapshots the database, migrates, and still rolls back cleanly if the gate fails.
- Token rows are unreadable without the key; every record carries a key ID so rotation tooling can be added later.
- Outbound HTTPS reaches only allowlisted hosts with verified certificates.
- A snapshot pulled by tranquility has been restored onto epiphany and the service came back healthy.
- Every test in the test plan passes.

**Entry criteria**

- [ ] M2 done: `v0.2.0` deployed through the gated path
- [ ] Decisions D1 to D9 confirmed
- [ ] crows-nest M5 PR1 and PR2 (backup source and nightly pull) applied before phase E (task D5)

**In scope:** `platform/sqlite`, migration runner, `platform/secrets`, the crypto port and adapter, the token store (no providers yet), `platform/http_client`, `class-t4 migrate | backup | restore | keys`, snapshot timer, deploy tool changes, contract updates.

**Out of scope:** calendar tables, providers and OAuth flows (M4); Prometheus rules and the restore-drill automation (M5).

## Decisions

Nine decisions shape M3. D5 and D6 change what the crows-nest reference currently says about backups; the reasoning is below the table.

| # | Decision | Recommended | Alternatives |
| --- | --- | --- | --- |
| D1 | SQLite access | SQLite C API behind a small RAII wrapper in `platform/sqlite`; one dedicated database thread, all queries posted to it | Drogon ORM; SQLiteCpp; a connection pool |
| D2 | Durability | `journal_mode=WAL`, `synchronous=FULL`, `foreign_keys=ON`, `busy_timeout=5000` | `synchronous=NORMAL` |
| D3 | Migrations | Forward-only SQL files per module, embedded in the binary; expand-only changes, so the previous release always runs on the new schema; the deploy tool snapshots, then migrates, before the restart | Down migrations; migrate at server startup |
| D4 | Token cipher | AES-256-GCM through OpenSSL, which is already linked; random 96-bit nonce per write; record bound to its row with associated data | libsodium XChaCha20-Poly1305 (new dependency) |
| D5 | Token key handling | 32 random bytes generated on epiphany, root-only file, loaded with `LoadCredential=`; backed up once to your password manager, never to restic | Key included in the restic backups |
| D6 | Backup model | class-t4 writes consistent snapshot files (hourly and before every migration) to a group-readable directory; crows-nest's `cn-backup` user streams `latest.db`, the manifest and the config through a forced command, with no sudo rule | Forced SSH command that streams `sqlite3 .backup` |
| D7 | Outbound HTTP | Deny by default: host allowlist and CA bundle path in config, HTTPS only, no redirects to unlisted hosts | Rely on the firewall alone |
| D8 | Health and the database | `/health` returns 503 when `SELECT 1` fails, so the M2 gate also catches database faults | Separate readiness endpoint |
| D9 | Release | `v0.3.0`, contract change noted in the changelog | None |

**Reasoning**

- **D3:** expand-only migrations mean a rollback never needs a database restore: the old binary simply ignores the new columns. The pre-migration snapshot is the safety net for the rare migration that goes wrong.
- **D4:** OpenSSL is already in the binary, so this adds no dependency or CVE surface. At household write volumes, random 96-bit nonces are far below any reuse risk.
- **D5 (changes crows-nest):** the crows-nest reference backs up the key alongside the database. Keeping the key out of restic means a compromise of tranquility's backups cannot decrypt anyone's calendar tokens. This covers stolen backups, not a live compromise of tranquility, whose Ansible key is root on epiphany; the ORR records that as an accepted risk. The cost is one manual step: the key goes into your password manager when it is generated or rotated.
- **D6 (changes crows-nest):** a streamed `sqlite3 .backup` needs the `sqlite3` CLI on epiphany and write access to the WAL files, which means extra privilege for the backup user. Snapshot files written by class-t4 itself need neither, and they double as the pre-migration safety net.
- **D7:** class-t4 links OpenSSL statically, so it will not find Debian's CA bundle on its own; the path has to be explicit either way, and an allowlist in the same block costs little.

## Where data and keys live

Only epiphany ever holds both the database and the token key; tranquility's backups hold snapshots without the key, and your password manager holds the key without the data.

&#91;embedded content: data and key locations · who can read what\]

The service reads the key through a systemd credential, so the class-t4 user itself cannot open the key file, and `cn-backup` can read snapshots but neither the live database nor the key.

## Specifications

These assume the recommended decisions and follow the naming already used on the M1 branch (nested snake\_case config objects, `https` and `ops` listeners). All of it lands in `docs/ops-contract.md` in task D4.

### Database

| Item | Value |
| --- | --- |
| File | `/var/lib/class-t4/class-t4.db` (plus `-wal`, `-shm`), class-t4 0600; directory 0750 |
| Pragmas at open | `journal_mode=WAL`, `synchronous=FULL`, `foreign_keys=ON`, `busy_timeout=5000` |
| Threading | One database thread owns the only connection; handlers post work to it and get a result back on their event loop |
| Unit | `UMask=0077`; `StateDirectory=class-t4` already creates the directory |

### Migrations

- Files: `app/**/migrations/NNNN_<name>.sql`, embedded at build time. Each module (and `platform`) owns its own sequence.
- Bookkeeping table `schema_migrations(module, version, name, sha256, applied_at)`, primary key `(module, version)`. Each file runs in one transaction.
- An applied migration whose checksum no longer matches the binary's copy is fatal (exit 1).
- Expand-only rule: a file containing `DROP`, `RENAME` or `ALTER ... DROP` fails CI unless its first line is `-- contract: <reason>`, and a contract migration ships only after a release that already stopped using the dropped item.
- `class-t4 migrate --config <path>` applies pending migrations; `--status` prints them and exits 0 (none pending) or 3 (pending).
- `serve` exits 1 if migrations are pending. Migrations unknown to the binary (after a rollback) are allowed, logged at `warn` and counted in a metric.

### Token store

Table `secrets(namespace TEXT, name TEXT, record BLOB NOT NULL, key_id BLOB NOT NULL, updated_at TEXT NOT NULL, PRIMARY KEY (namespace, name))`, owned by `platform/token_store`. Each module writes under its own namespace, so one store holds OAuth tokens, CalDAV passwords and feed URLs.

| Record byte range | Field |
| --- | --- |
| 0 | Format version, `0x01` |
| 1 to 8 | Key ID: first 8 bytes of SHA-256 of the key |
| 9 to 20 | Random 96-bit nonce |
| 21 to end minus 16 | AES-256-GCM ciphertext |
| Last 16 | GCM tag |

Associated data is `class-t4/secrets/<namespace>/<name>`, so a record copied to another row fails to decrypt.

### Token key

| Item | Value |
| --- | --- |
| File | `/etc/class-t4/credentials/token-key`, 32 random bytes, root 0600 |
| Delivery | `LoadCredential=token-key:/etc/class-t4/credentials/token-key`; read from `$CREDENTIALS_DIRECTORY` |
| Off-host copy | Base64 and key ID in your password manager; never in restic, the repo or crows-nest |
| Rotation | Deferred past v1.0. Planned shape: `class-t4-deploy rotate-key <new-key-file>`: stop, snapshot, run `class-t4 keys rewrap` in a transient unit with both keys as credentials (`systemd-run -p LoadCredential=...`), move the new key into place, start, gate |
| Inspection | `class-t4 keys status`: loaded key ID and row counts per key ID |

### Snapshots and restore

| Item | Value |
| --- | --- |
| Command | `class-t4 backup --config <path> --reason hourly\|pre-migrate\|manual` |
| Method | SQLite online backup API to a `.tmp` file, `fsync`, `PRAGMA integrity_check` on the copy, rename, update `latest.db` and write the manifest `latest.json` (UTC time, class-t4 version, schema version per module, SHA-256) |
| Directory | `/var/lib/class-t4/backups/`, owner class-t4, group `class-t4-backup`, 0750; files 0640 |
| Names | `class-t4-<UTC yyyymmddThhmmssZ>-<reason>.db` |
| Retention | 48 hourly and the last 5 pre-migrate snapshots |
| Schedule | `class-t4-snapshot.timer`, `OnCalendar=hourly`, `RandomizedDelaySec=5m`; the service runs as class-t4 with the main unit's hardening and no key |
| Restore | `class-t4-deploy restore <snapshot>`: stop, integrity-check the snapshot, move the live database aside, copy in, start, gate; on a gate failure, put the original back |

The `class-t4-backup` group comes from the sysusers file; crows-nest adds `cn-backup` to it, so it can read snapshots but not the live database or the key.

### Outbound HTTPS

| Rule | Value |
| --- | --- |
| Port | `core` interface `http_client`; Drogon `HttpClient` adapter in `platform/http_client` |
| Schemes | `https` only |
| Hosts | Exact match against `outbound.allow_hosts`, checked before connecting; port 443 unless listed as `host:port`; after DNS resolution, private, loopback and link-local addresses are refused |
| Redirects | Up to 3, each to an allowlisted host |
| TLS | Verification on, CA bundle from `outbound.ca_file` |
| Limits | `outbound.timeout_seconds` (default 15), 10 MiB response cap |

### Config additions

```json
{
  "database": { "path": "/var/lib/class-t4/class-t4.db" },
  "backup": { "dir": "/var/lib/class-t4/backups", "keep_hourly": 48, "keep_premigrate": 5 },
  "outbound": {
    "ca_file": "/etc/ssl/certs/ca-certificates.crt",
    "allow_hosts": [],
    "timeout_seconds": 15
  }
}
```

The allowlist stays empty on epiphany until M4.

### Deploy tool changes

| Change | Behavior |
| --- | --- |
| `activate` | If the new binary reports pending migrations: `backup --reason pre-migrate`, then `migrate`, both as class-t4, before the swap. A failed migration exits 12 with nothing swapped |
| `restore <snapshot>` | As specified above |
| `rotate-key <file>` | As specified above |
| New exit code 12 | Migration failed; nothing activated; the old release keeps serving on the expand-only schema |

### New metrics

| Metric | Labels | Used by |
| --- | --- | --- |
| `class_t4_db_schema_version` | `module` | Restore checks, M5 |
| `class_t4_db_unknown_migrations` | none | Rollback visibility |
| `class_t4_backup_last_success_timestamp_seconds` | `reason` | M5 stale-backup alert |
| `class_t4_token_records` | `key_id` | Rotation checks |

## Execution plan

24 tasks in 6 phases (E4 and the rewrap half of B4 are deferred past v1.0 with key rotation), about 15 sessions in class-t4 plus about 1 in crows-nest. Phase C has no dependency on A or B, so it can run alongside them.

| Phase | Tasks | Sessions | Your touchpoints | Exits with |
| --- | --- | --- | --- | --- |
| A SQLite platform and migrations | A1 to A5 | 4 | PR review | Database thread, migration runner, `migrate`, health check |
| B Secrets, crypto, token store | B1 to B4 | 3 | PR review | Encrypted token store with rewrap |
| C Outbound HTTP | C1 to C3 | 2 | PR review | Allowlisted, verified HTTPS client |
| D Backups, restore, contract | D1 to D5 | 3 here, \~1 in crows-nest | crows-nest change | Snapshots, restore, deploy tool changes, contract |
| E On-host rollout and drills | E1 to E4 | 2 | All four | Key in place, migrations, restore and rotation proven on epiphany |
| F Docs and release | F1 to F3 | 1.5 | F3 | `v0.3.0` signed and running |

- **Critical path:** A1 → A2 → A3 → A4 → D1 → D3 → E2.
- **Parallel:** B starts after A2; C starts any time; F1 runbooks are written during phase E.
- **Workflow:** unchanged from M1 and M2. Claude Code never handles the token key; tests generate throwaway keys in temp directories.

Task format below: **ID. Name** · size · owner · depends on, then details and validation.

## Tasks

### A: SQLite platform and migrations

Phase A gives every later module a safe way to store data and evolve its schema, with SQLite visible only inside `platform/sqlite` and module adapters.

**A1. SQLite wrapper** · M · Claude · M2

- **Deliverables:** `sqlite3` in `vcpkg.json` (static); `app/platform/sqlite/` with `connection`, `statement` and `transaction` types.
- **Details:** `connection` opens with the D2 pragmas and fails if WAL cannot be enabled. `statement` covers prepare, typed bind, step and column reads. `transaction` rolls back in its destructor unless committed. Every SQLite error maps to a `core` error with the SQLite code and message. No exceptions cross the wrapper.
- **Validate:** U-01 to U-04.

**A2. Database thread** · M · Claude · A1

- **Deliverables:** `platform/sqlite/database`.
- **Details:** owns the only connection on a `std::jthread` with a FIFO task queue. `submit(fn)` returns a future for CLI use; `post(fn, callback)` delivers the result back on the calling Drogon event loop for handlers. On SIGTERM the queue drains before the connection closes.
- **Validate:** U-05, U-06.

**A3. Migration runner** · M · Claude · A2

- **Deliverables:** `platform/sqlite/migrations`, a CMake function that embeds `migrations/*.sql` per module, the platform's own `0001_schema_migrations.sql`.
- **Details:** applies pending files in `(module, version)` order, each in its own transaction, recording name and SHA-256. Checksum mismatch on an applied migration is fatal; migrations the binary does not know are reported, not fatal.
- **Validate:** U-07 to U-10.

**A4. `migrate` command and startup check** · S · Claude · A3

- **Details:** `class-t4 migrate --config <path>` and `--status` per the spec; `serve` exits 1 with a clear message if anything is pending; the two schema metrics are exported.
- **Validate:** I-01 to I-03.

**A5. Health check and migration lint** · S · Claude · A2, A3

- **Deliverables:** `SELECT 1` in `/health` (503 on failure, through the database thread with a 1-second timeout), registered through a health-check interface that each later module adds its own checks to; `scripts/check-migrations.sh` in the CI lint job.
- **Validate:** I-04, S-01.

### B: Secrets, crypto and the token store

Phase B makes stored OAuth tokens useless without the key, and makes the key replaceable without re-consent.

**B1. Secrets loader** · S · Claude · A2

- **Deliverables:** `app/platform/secrets/`.
- **Details:** reads `token-key` from `$CREDENTIALS_DIRECTORY`; the key must be exactly 32 bytes or startup fails with exit 1. Computes the key ID. Holds the key in a buffer that is wiped with `OPENSSL_cleanse` on shutdown and is never logged or exported. A `--credentials-dir` flag exists for tests and local runs only. The unit's commented LoadCredential line from M1 becomes live.
- **Validate:** U-11 to U-13.

**B2. Crypto port and OpenSSL adapter** · M · Claude · B1

- **Deliverables:** `core` interface `sealed_box` (`seal(plaintext, aad)`, `open(record, aad)`); `app/platform/crypto/` adapter.
- **Details:** AES-256-GCM through the OpenSSL EVP API, nonce from `RAND_bytes`, record format v1 exactly as specified. `open` checks the version byte and key ID before decrypting and returns a typed error for each failure (unknown version, wrong key, tag mismatch).
- **Validate:** U-14 to U-18.

**B3. Token store** · M · Claude · A3, B2

- **Deliverables:** `app/platform/token_store/` with its migration `0001_secrets.sql`.
- **Details:** `put`, `get`, `remove` and `count_by_key_id`, all through the database thread. Plaintext is held in a `secret_string` type that wipes on destruction and has no stream or format output. Exports `class_t4_token_records`. On first start it writes a canary row (namespace `platform`, name `canary`); every start opens it and exits 1 if it fails, so a wrong key after a restore or rotation is caught at the health gate instead of at the next calendar sync.
- **Validate:** I-05, I-06, I-17.

**B4. `keys` commands** · S · Claude · B3

- **Details:** `class-t4 keys status` prints the loaded key ID and row counts per key ID. Deferred past v1.0: `class-t4 keys rewrap` reads `token-key-old` and `token-key-new` from `$CREDENTIALS_DIRECTORY`, re-seals every row under the new key in one transaction, checks every row opens with the new key, then commits. Rows already under the new key are skipped, so a rerun is safe.
- **Validate:** I-07, I-08.

### C: Outbound HTTP client

Phase C gives M4's providers one way out of epiphany, and that way refuses anything not explicitly allowed.

**C1. `http_client` port and policy** · S · Claude · M2

- **Deliverables:** `core` interface `http_client` with request and response types and typed errors (`not_allowed`, `tls`, `timeout`, `too_large`, `transport`); a pure `egress_policy` in `core` that decides allow or deny for a URL.
- **Details:** the policy implements the scheme, host, port and redirect rules from the spec, so it is fully unit-testable with no network.
- **Validate:** U-19 to U-21.

**C2. Drogon adapter** · M · Claude · C1

- **Deliverables:** `app/platform/http_client/`.
- **Details:** checks `egress_policy` before every connection; certificate validation on; automatic redirects off, with each redirect re-checked against the policy (at most 3); enforces the timeout and the 10 MiB cap while reading. The CA bundle is set through trantor's TLS policy if it exposes a CA path, otherwise through `Environment=SSL_CERT_FILE=` in the unit; C3 proves which.
- **Validate:** I-09 to I-13.

**C3. Outbound integration tests** · S · Claude · C2

- **Deliverables:** `tests/integration/outbound_*`.
- **Details:** a local HTTPS test server with a throwaway CA serves fixtures for success, slow responses, oversized bodies and redirects. Tests run with that CA as `outbound.ca_file` and the test server's name in the allowlist.
- **Validate:** I-09 to I-13 green on `host-debug`.

### D: Backups, restore and contract

Phase D makes every database change reversible on the host and hands crows-nest a file it can pull without privileges.

**D1. `backup` command** · M · Claude · A3

- **Details:** online backup API from its own connection, so it runs safely while the server writes; then `fsync`, `PRAGMA integrity_check` on the copy, atomic rename, `latest.db` and `latest.json` update, and retention per reason. The server's `/metrics` reads the newest snapshot time per reason for `class_t4_backup_last_success_timestamp_seconds`.
- **Validate:** I-14 to I-16.

**D2. Snapshot timer, group and directory** · S · Claude · D1

- **Deliverables:** `deploy/systemd/class-t4-snapshot.service` and `.timer`; `class-t4-backup` group in the sysusers file; `deploy/tmpfiles.d/class-t4.conf` with `d /var/lib/class-t4/backups 2750 class-t4 class-t4-backup -`. Tarball adds all three under `share/`.
- **Details:** the setgid directory makes new snapshots inherit the group; the snapshot service uses `UMask=0027` so files land at 0640, while the main service keeps `UMask=0077`.
- **Validate:** S-02, S-03.

**D3. Deploy tool changes** · M · Claude · A4, D1, D2, B4

- **Details:** `activate` gains the pre-migrate snapshot and `migrate` step and exit code 12; `install` and `activate` also install the snapshot units and tmpfiles entry; a new `restore` command per the spec (`rotate-key` is deferred past v1.0).
- **Validate:** T-17 to T-22 in the deploy test suite.

**D4. Ops contract for M3** · S · Claude · D1 to D3

- **Details:** adds the database path and modes, the snapshot directory, group, names, retention and schedule, the key path with "never backed up by crows-nest", the new metrics, exit code 12, the `/health` database semantics, and the empty outbound allowlist.
- **Validate:** your review; the M1 contract test (every documented metric appears in a test) stays green.

**D5. crows-nest backup changes** · external · crows-nest · D4

- **Details:** crows-nest M5 PR1 and PR2: `cn-backup` joins `class-t4-backup`; its forced command streams `latest.db`, `latest.json` and the config, with no sudo rule; `backup_pull` runs nightly through restic's `--stdin-from-command`; the restore drill checks the manifest hashes, `PRAGMA integrity_check` and `schema_migrations` with the `sqlite3` CLI on tranquility. Update the crows-nest reference: the key is no longer part of the backup set.
- **Validate:** H-05.

### E: On-host rollout and drills

Phase E is yours. The release sequence is `v0.3.0-rc.1` (first deploy with migrations), `0.3.0-drill.1` (a migration that must fail), then `v0.3.0`.

**E1. Generate and store the token key** · S · **you** · D3

- **Details:** on epiphany as root:

```
install -d -m 0700 /etc/class-t4/credentials
head -c 32 /dev/urandom > /etc/class-t4/credentials/token-key
chmod 0600 /etc/class-t4/credentials/token-key
base64 /etc/class-t4/credentials/token-key
```

Store the base64 value in your password manager. The key ID is recorded after E2 from `class-t4 keys status`.

- **Validate:** H-01.

**E2. First deploy with migrations** · S · **you** · E1, D5

- **Details:** deploy `v0.3.0-rc.1` with the playbook. `activate` takes a pre-migrate snapshot, migrates, swaps and gates. Run `class-t4 keys status` and add the key ID next to the key in your password manager.
- **Validate:** H-02 to H-04.

**E3. Restore and migration-failure drills** · S · **you** · E2

- **Details:**
  1. Copy the latest snapshot from tranquility's restic repo back to epiphany and run `class-t4-deploy restore` with it.
  2. Build `0.3.0-drill.1` from `drill/fail-migration` (a migration with a syntax error), sign and stage it like the M2 drill, and deploy it. Expect exit 12 and `rc.1` still serving.
- **Validate:** H-05, H-06.

**E4. Key rotation drill (deferred past v1.0)** · S · **you** · E2

- **Details:** generate a new key file as in E1, store it in your password manager, run `class-t4-deploy rotate-key <file>`, then check `keys status` shows only the new key ID. Remove the old key from the password manager only after the next snapshot has been pulled.
- **Validate:** H-07.

### F: Docs and release

Phase F records the data and key procedures you just proved and ships `v0.3.0`.

**F1. Runbooks** · S · Claude drafts from your E-phase notes, you review · E1 to E4

- **Deliverables:** `docs/runbooks/key-management.md` (generate, store, rotate, recover), `restore.md`, `writing-migrations.md` (expand-only rule, contract migrations), and an updated `upgrade.md` covering exit 12.
- **Validate:** your review; F3 follows `upgrade.md`.

**F2. ADRs and changelog** · S · Claude · E3

- **Deliverables:** `docs/adr/0009-database-access.md`, `0010-migration-policy.md`, `0011-token-encryption-and-key.md`, `0012-backup-model.md`; a `v0.3.0` section in `CHANGELOG.md` that calls out the contract changes.
- **Validate:** your PR review.

**F3. Release and deploy `v0.3.0`** · S · **you** · F1, F2

- **Details:** the M1 release procedure, then `upgrade.md` with the deploy playbook.
- **Validate:** H-08.

## Test plan

55 tests are listed for M3 (I-07, I-08, T-22 and H-07 are deferred with key rotation): 21 unit, 17 integration, 3 build checks, 6 new deploy-tool tests and 8 host tests. The crypto and egress tests concentrate on the failure cases, since those are the ones that protect calendar tokens.

### Unit tests (GoogleTest; `host-debug` and under qemu)

| ID | Test | Task |
| --- | --- | --- |
| U-01 | Opening a database sets WAL, `synchronous=FULL`, foreign keys and the busy timeout (read back) | A1 |
| U-02 | A transaction dropped without commit leaves no writes | A1 |
| U-03 | Bind and read round trip for int64, text, blob and null | A1 |
| U-04 | A constraint violation becomes a `core` error carrying the SQLite code; no exception escapes | A1 |
| U-05 | Tasks run in submission order on one thread | A2 |
| U-06 | Shutdown runs every queued task before closing | A2 |
| U-07 | Fresh database: every migration applied in `(module, version)` order | A3 |
| U-08 | Running migrations twice changes nothing | A3 |
| U-09 | An edited, already-applied migration is a fatal checksum mismatch | A3 |
| U-10 | A failing migration rolls back only itself; the error names the file | A3 |
| U-11 | Missing `token-key` credential: startup error naming it | B1 |
| U-12 | 31-byte and 33-byte keys rejected | B1 |
| U-13 | Key ID is deterministic; captured logs contain neither the key nor its base64 | B1 |
| U-14 | `seal` then `open` returns the plaintext | B2 |
| U-15 | Flipping any byte of nonce, ciphertext or tag makes `open` fail | B2 |
| U-16 | A record opened with another row's associated data fails | B2 |
| U-17 | Wrong key fails on the key ID check, before decryption | B2 |
| U-18 | 10,000 seals give 10,000 distinct nonces; an unknown version byte is rejected | B2 |
| U-19 | Policy allows only `https`, listed hosts and port 443 or a listed port | C1 |
| U-20 | Policy handles trailing dots, letter case, IP literals and `https://allowed@other` tricks; an allowed name that resolves to a private, loopback or link-local address is refused | C1 |
| U-21 | A fourth redirect, or one to an unlisted host, is denied | C1 |

### Integration tests (`tests/integration/`, `host-debug`)

| ID | Test | Task |
| --- | --- | --- |
| I-01 | `migrate` on a fresh database exits 0; `--status` then exits 0 | A4 |
| I-02 | `serve` with a pending migration exits 1 with a clear message | A4 |
| I-03 | Database carrying a migration the binary does not know: `serve` starts, logs `warn`, metric reads 1 | A4 |
| I-04 | `/health` returns 503 while the database is unreadable and 200 after it is fixed | A5 |
| I-05 | Token `put` then `get` returns the same value across a restart | B3 |
| I-06 | The database and WAL files never contain a stored token's plaintext | B3 |
| I-17 | Starting with a different key fails the canary and exits 1 | B3 |
| I-07 | `keys rewrap` moves every row to the new key; the old key is no longer needed | B4 |
| I-08 | Killing `keys rewrap` mid-run leaves the database unchanged; a rerun completes | B4 |
| I-09 | Request to the allowlisted test server with the test CA returns 200 | C2 |
| I-10 | Unlisted host returns `not_allowed` and the test server sees no connection | C2 |
| I-11 | Server cert from another CA returns `tls` | C2 |
| I-12 | Slow response returns `timeout`; oversized body returns `too_large` | C2 |
| I-13 | Redirect to an unlisted host returns `not_allowed` | C2 |
| I-14 | Backup taken during continuous writes passes `integrity_check` and matches a committed state | D1 |
| I-15 | Retention keeps 48 hourly and 5 pre-migrate; `latest.db` points at the newest | D1 |
| I-16 | Snapshot files are written with mode 0640 | D1 |

### Build checks (CI lint job)

| ID | Test | Task |
| --- | --- | --- |
| S-01 | Migration lint fails on `DROP` without the contract marker and passes with it | A5 |
| S-02 | `systemd-analyze verify` on the snapshot service and timer; snapshot service exposure 2.5 or lower | D2 |
| S-03 | `systemd-sysusers` and `systemd-tmpfiles` in a temp root create the group and a 2750 backups directory | D2 |

### Deploy tool tests (added to `tests/deploy/run.sh`)

| ID | Test | Task |
| --- | --- | --- |
| T-17 | With pending migrations, `activate` snapshots, then migrates, then swaps (call order recorded) | D3 |
| T-18 | Failing migration: exit 12, links unchanged | D3 |
| T-19 | `restore` stops, checks, swaps the database, starts and passes the gate; the original is kept aside | D3 |
| T-20 | `restore` whose gate fails puts the original back and exits 10 | D3 |
| T-21 | `restore` of a corrupt snapshot exits 3 and changes nothing | D3 |
| T-22 | `rotate-key` passes both keys as credentials to the rewrap unit; if rewrap fails, the old key stays and the service comes back on it | D3 |

### Host tests (you, on epiphany)

| ID | Test | Pass when | Task |
| --- | --- | --- | --- |
| H-01 | Key file checks | 32 bytes, root 0600; `sudo -u class-t4 cat` is denied; the service still starts | E1 |
| H-02 | After the `rc.1` deploy | A pre-migrate snapshot exists; `migrate --status` exits 0; `keys status` shows the key ID and the canary | E2 |
| H-03 | Permissions | Database 0600 class-t4; backups directory 2750 class-t4:class-t4-backup; snapshots 0640 | E2 |
| H-04 | Snapshot timer | Listed in `systemctl list-timers`; a new hourly snapshot within 65 minutes; the metric timestamp moves | E2 |
| H-05 | Restore drill | `cn-backup` can read snapshots but not the database or key; restore from the restic copy passes the gate and the canary | E3, D5 |
| H-06 | Migration-failure drill | Exit 12; `rc.1` serving; `PRAGMA integrity_check` ok | E3 |
| H-07 | Rotation drill | `keys status` shows only the new key ID; service healthy | E4 |
| H-08 | `v0.3.0` deployed by `upgrade.md` | `class-t4-deploy status` shows `0.3.0` current | F3 |

## Acceptance and sign-off

M3 is done when `v0.3.0` is serving on epiphany with its database, key and snapshots in place and every box below is ticked; you are the approver.

- [ ] All 24 tasks closed (D5 closed in crows-nest)
- [ ] All 55 tests pass where they are meant to run
- [ ] `docs/ops-contract.md` updated and mirrored in crows-nest; the crows-nest reference no longer lists the key in the backup set
- [ ] Token key and its ID stored in your password manager
- [ ] Runbooks, ADRs 0009 to 0012 and the `v0.3.0` changelog merged
- [ ] `v0.3.0` published (tarball, `.minisig`, `SHA256SUMS`, SBOM) and deployed by `upgrade.md`

## Risks

The highest-impact risk is losing the token key: everything else in M3 can be rebuilt, but a lost key means every calendar owner has to consent again.

| Risk | Trigger | Response |
| --- | --- | --- |
| Token key lost | epiphany disk failure with no password-manager copy | E1 makes the copy part of the task; the canary proves a restored key matches |
| Non-expand migration slips through | A rename or drop without the marker | S-01 lint; ADR 0010 review checklist |
| SQLite writes stall Drogon threads | Slow queries on the IO loop | Single database thread by design (D1); handlers never block on SQLite |
| Static OpenSSL ignores the system CA bundle | I-09 fails against a real host in M4 | Explicit `ca_file` (D7); `SSL_CERT_FILE` fallback in C2 |
| `synchronous=FULL` costs too much on the Pi | Write latency during M4 sync | Measure in M5 load test; revisit only with numbers |
| Snapshot timer and server contend for the database | Hourly backup during a sync | Online backup API copies in steps; busy timeout covers short locks |

## Open items

- [ ] D1 to D9 confirmed on 2026-11-12; crows-nest M5 now matches D5 and D6
- [ ] Snapshot retention (48 hourly, 5 pre-migrate): confirm, given the database is small
- [ ] Decided: `backup_pull` takes `latest.db` nightly (RPO 24 hours)
- [ ] Decided: crows-nest installs the `sqlite3` CLI on tranquility (M5 T11)

## Change log

| Date | Change |
| --- | --- |
| 2026-11-12 | Created from the project plan, the reference doc, the crows-nest reference and the M2 doc |
| 2026-11-12 | Cross-repo review applied: generic secrets table with namespaces; snapshot manifest latest.json; cn-backup forced command, nightly pull; resolved-address egress check; per-module health checks; key rotation tooling deferred past v1.0 |
