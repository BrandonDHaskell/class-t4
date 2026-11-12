# class-t4 M4: Calendar Aggregator

Nov 12, 2026 · @Brandon

## Overview

M4 delivers the first real service: a read-only household calendar that merges ICS feeds, Apple iCloud, Google and Microsoft calendars and serves them over `/api/v1/calendar` on the LAN. It ships as three releases, so each new provider goes through the full deploy path on its own. Roadmap context: [class-t4: Project Plan](https://claude.ai/artifact/Eoy2RvQxbejN5U8tmEtY6J).

| Release | Adds | Proves |
| --- | --- | --- |
| `v0.4.0` | Calendar domain, storage, sync engine, API with its read token, admin CLI, ICS provider (Google included, through its secret iCal address) | The whole pipeline with the simplest, credential-free provider |
| `v0.5.0` | CalDAV provider for Apple iCloud | Incremental sync and stored passwords |
| `v0.6.0` | OAuth device flow, Microsoft provider | Consent on epiphany and refresh-token handling |

**Exit criteria**

- `v0.6.0` runs on epiphany with at least one live source per provider, each syncing on schedule.
- `GET /api/v1/calendar/events` returns every household member's events for a requested range, tagged with owner and source.
- A revoked OAuth grant puts its source into `needs_reconsent` without affecting other sources.
- Outbound traffic reaches only provider hosts and configured feed hosts.
- Every test in the test plan passes.

**Entry criteria**

- [ ] M3 done: `v0.3.0` with database, token store and outbound client running
- [ ] Decisions D1 to D10 confirmed
- [ ] Provider spikes S1 to S4 done (they gate the provider tasks, not phases A to C)

**Out of scope:** writing to any calendar, the SPA, notifications, sharing outside the LAN, alert rules (M5).

## Architecture

All four providers end in the same `occurrences` table, so the API, the privacy rules and the tests never need to know which provider an event came from.

&#91;embedded content: calendar data path · outside services to LAN clients\]

Arrows show where data flows. Requests flow the other way: the sync engine asks an adapter, which calls out through `http_client`, which refuses any host not on the allowlist.

## Decisions

Eleven decisions shape M4. D7 relaxes one line of the reference doc (the host firewall as the egress gate); the reasoning is below the table.

| # | Decision | Recommended | Alternatives |
| --- | --- | --- | --- |
| D1 | iCalendar parsing and recurrence | libical from vcpkg, used only inside an adapter | Hand-written parser and RRULE expander |
| D2 | Storage model | Store expanded occurrences inside a rolling window (90 days back, 365 ahead); the API reads only occurrences | Store masters and expand per request |
| D3 | Where recurrence is expanded | Locally with libical for ICS (Google feeds included) and CalDAV, which deliver RRULEs; server-side for Microsoft, whose recurrence model is not RRULE (spikes confirm) | Local expansion for all four |
| D4 | Privacy | Store only title, times, all-day flag, location, status, busy and visibility; private events become "Busy" with no title or location; never descriptions, attendees or attachments | Store full event bodies |
| D5 | Admin path | `class-t4ctl` wrapper runs the CLI in a transient systemd unit as class-t4 with the needed credentials, then sends the server `SIGHUP` to reload sources | Admin socket or admin HTTP API |
| D6 | OAuth client registrations | Root-only systemd credential `oauth-clients` (JSON), never in the repo config | In `deploy/epiphany/class-t4.json` |
| D7 | Egress control | Effective allowlist = provider hosts from config plus hosts that modules supply through an egress hook (the calendar module returns its sources' hosts); one-label wildcards (`*.icloud.com`) allowed; the host firewall has no output chain and the app enforces hosts | Firewall rules per provider hostname |
| D8 | API access | One read token from a root-only systemd credential (`api-token`), required as a bearer token on every `/api/v1` request from `v0.4.0` | No authentication, LAN only |
| D9 | Microsoft account type | Personal accounts (`consumers` tenant) | Work or school (`organizations`), or both (`common`) |
| D10 | Releases | One per provider that needs code: `v0.4.0` (ICS), `v0.5.0` (CalDAV), `v0.6.0` (device flow and Microsoft) | One `v0.4.0` at the end |
| D11 | Google access | Secret iCal address through the ICS provider | Google API provider with the desktop loopback flow over an SSH tunnel (deferred past v1.0) |

**Reasoning**

- **D2:** the calendar view is always "what happens between these two dates". Pre-expanded occurrences make that one indexed range query, and the window bounds both storage and memory on a 4GB Pi.
- **D4:** class-t4 only needs to show who is busy when and with what. Storing less makes the database and its snapshots less sensitive, and it settles how private events display.
- **D5:** the server stays the only process that schedules syncs, with no new listening surface. The CLI already exists, and a transient unit gives it the same user, credentials and hardening as the service.
- **D7 (relaxes the reference doc):** provider IP addresses change constantly, so nftables rules by hostname go stale. The app-level allowlist checks the actual hostname, and it refuses unlisted hosts before connecting (proven in M3).
- **D10:** a provider that misbehaves in production then rolls back alone, without taking the other three with it.

* **D8 (reverses the earlier no-auth choice):** `v1.0.0` freezes `/api/v1` as additive only, so adding auth later would be a breaking change. A token now costs one credential and one check.
* **D11:** Google limits its device flow to sign-in, Drive file and YouTube scopes, so `calendar.readonly` cannot be requested that way. The secret iCal address needs no OAuth client, no client secret and no token refresh.

## Specifications

These assume the recommended decisions. Provider behaviors marked "spike" are confirmed or corrected by phase S before the provider tasks start.

### Data model (calendar module migrations)

| Table | Key columns | Notes |
| --- | --- | --- |
| `owners` | `id`, `name` (unique), `color` | Household members |
| `sources` | `id`, `owner_id`, `provider` (`ics`, `caldav`, `microsoft`), `label`, `host`, `account_hint`, `status`, `sync_state` (JSON), `next_run_at`, `last_success_at`, `last_error`, `consecutive_failures`, sync\_requested\_at | `host` feeds the egress allowlist; no secrets here |
| `calendars` | `id`, `source_id`, `remote_id`, `name`, `color`, `included` (default 1) | Unique on `(source_id, remote_id)` |
| `occurrences` | `calendar_id`, `uid`, `instance_key`, `start_utc`, `end_utc`, `all_day`, `start_date`, `end_date`, `tzid`, `title`, `location`, `status`, `busy`, `private`, `content_hash` | Unique on `(calendar_id, uid, instance_key)`; indexed on `start_utc` and `end_utc` |

Secrets per source live in the M3 token store under the source ID: OAuth refresh tokens, the CalDAV app-specific password, and full ICS URLs (private feed URLs carry tokens). Access tokens stay in memory only. The store has no foreign key to `sources`; removing a source deletes its token-store entries in the same transaction as `sources` its other rows. Entries use namespace `calendar` and name `source:<id>`.

### Recurrence and time

- Window: 90 days back to 365 days ahead, slid nightly at 03:00 household time by re-expanding every source.
- Timed events stored as UTC seconds plus the original `tzid`; all-day events as local dates, end exclusive (iCalendar rules).
- Floating times and all-day boundaries use modules.`calendar.timezone` (default `America/Los_Angeles`).
- Exceptions (`RECURRENCE-ID`, `EXDATE`, Google exception instances) replace or remove the matching occurrence.

### Privacy mapping

| Source signal | Stored as |
| --- | --- |
| `CLASS:PRIVATE` or `CONFIDENTIAL`; Google `visibility` private or confidential; Microsoft `sensitivity` private or confidential | `private=1`, title "Busy", no location |
| `TRANSP:TRANSPARENT`; Google `transparency` transparent; Microsoft `showAs` free | `busy=0` |
| `STATUS:CANCELLED`; cancelled or deleted instances | Removed |

### Sync

| Provider | Interval | Incremental mechanism | On change |
| --- | --- | --- | --- |
| ICS | 30 min | Conditional GET (`ETag`, `Last-Modified`) | Re-parse the feed, replace the source's occurrences |
| CalDAV | 15 min | `sync-collection` REPORT with a sync token; ctag fallback (spike) | Fetch changed resources, re-expand affected UIDs |
| Microsoft | 10 min | `calendarView` delta over the window (spike) | Apply instance changes; window slide forces a full resync |

- Jitter of plus or minus 10%; at most 2 syncs at once.
- Failure backoff doubles the interval up to 6 hours; `Retry-After` and HTTP 429 are honored.
- Each calendar's changes commit in one transaction; a failed sync leaves the last good data in place.
- A source is stale when its last success is older than 3 intervals.

**Source states:** `active` → `error` on transient failures (keeps retrying) → `active` on success; `active` → `needs_reconsent` on `invalid_grant` or a CalDAV 401 (stops syncing); `paused` by admin. `source reconsent` returns a source to `active`; `source remove` deletes its calendars, occurrences and secrets.

### Providers and egress

| Provider | Hosts on the allowlist | Auth | Scope |
| --- | --- | --- | --- |
| ICS | Each feed's host, added when the source is added (`calendar.google.com` for Google's secret iCal addresses) | None, or a token inside the URL | Read |
| CalDAV (iCloud) | `caldav.icloud.com`, `*.icloud.com` (partition hosts, spike) | Apple ID and app-specific password | Read by usage |
| Microsoft | `login.microsoftonline.com`, `graph.microsoft.com` | Device flow, `consumers` tenant | `Calendars.Read offline_access` |

The allowlist checks happen in the M3 egress policy, extended with one-label wildcards and with source hosts read from the database at each sync.

### Credentials

| Credential | File (root 0600) | Contents |
| --- | --- | --- |
| `token-key` | `/etc/class-t4/credentials/token-key` | M3 token key |
| `oauth-clients` | `/etc/class-t4/credentials/oauth-clients.json` | `{"microsoft":{"client_id":"…"}}` |
| api-token | /etc/class-t4/credentials/api-token | 32 random bytes in base64; the bearer token LAN clients send |

### Admin CLI

Run on epiphany as root through `class-t4ctl`, which wraps `systemd-run --uid=class-t4 --pty --wait --collect` with both credentials, then sends `SIGHUP` to the service.

| Command | Behavior |
| --- | --- |
| `owner add <name> [--color]`, `owner list` | Manage household members |
| `source add ics --owner <name> --label <text>` | Prompts for the feed URL without echo |
| `source add caldav --owner <name> --label <text> --apple-id <email>` | Prompts for the app-specific password; discovers calendars |
| `source add microsoft --owner <name> --label <text>` | Device flow: prints the URL and code, waits up to 15 minutes |
| `source list`, `source calendars <id> [--include\|--exclude <calendar>]` | Inspect and choose calendars |
| `source sync\|pause\|resume\|reconsent\|remove <id>` | Operate one source |

Exit codes: 0 success, 2 usage, 6 provider or auth failure, 7 consent timed out.

### API v1

| Endpoint | Returns |
| --- | --- |
| `GET /api/v1/calendar/events?from=YYYY-MM-DD&to=YYYY-MM-DD[&owner=<id>]` | Occurrences overlapping the range (at most 62 days, inside the window) |
| `GET /api/v1/calendar/owners` | Owners with colors |
| `GET /api/v1/calendar/sources` | Label, provider, owner, status, last success, stale flag; never URLs, accounts or secrets |

```json
{
  "from": "2026-11-01", "to": "2026-12-01", "timezone": "America/Los_Angeles",
  "events": [{
    "id": "opaque", "owner": {"id": 1, "name": "Brandon", "color": "#3b82f6"},
    "source": {"id": 2, "label": "Work", "provider": "google"},
    "calendar": {"id": 5, "name": "Primary"},
    "title": "Dentist", "start": "2026-11-12T09:00:00-08:00", "end": "2026-11-12T10:00:00-08:00",
    "all_day": false, "location": null, "status": "confirmed", "busy": true, "private": false
  }],
  "stale_sources": []
}
```

All-day events use `"start": {"date": "2026-11-12"}`. Responses carry a weak `ETag` and answer `If-None-Match` with 304. Errors are `{"error":{"code":"bad_range","message":"…"}}` with 400, 401, 404 or 503. Every `/api/v1` request needs `Authorization: Bearer <token>`, checked against the `api-token` credential; a missing or wrong token returns 401. The contract is `docs/api/openapi.yaml`.

### New metrics

| Metric | Labels |
| --- | --- |
| `class_t4_calendar_sync_runs_total` | `provider`, `source_id`, `result` |
| `class_t4_calendar_sync_last_success_timestamp_seconds` | `source_id` |
| `class_t4_calendar_sync_duration_seconds` (histogram) | `provider` |
| `class_t4_calendar_source_status` | `source_id`, `status` |
| `class_t4_calendar_occurrences` | `source_id` |
| `class_t4_outbound_requests_total` | `host`, `code` |

## Execution plan

30 tasks in 8 phases, about 35 sessions: M4 is the largest milestone, so it is cut into three releases that each stand alone. Spikes and provider registrations run early and in parallel, because they depend on outside services and on you.

| Phase | Tasks | Sessions | Your touchpoints | Needed for |
| --- | --- | --- | --- | --- |
| S Provider spikes | S1 to S4 | 4 | Run S2 to S4 with your accounts | S1, S3: `v0.4.0`; S2: `v0.5.0`; S4: `v0.6.0` |
| A Domain and storage | A1 to A4 | 5 | PR review | `v0.4.0` |
| B Sync engine | B1 to B4 | 4 | PR review | `v0.4.0` |
| C API and admin | C1 to C4 | 4 | PR review | `v0.4.0` |
| D Providers | D1 to D5 | 11 | PR review | One provider per release |
| E Contract, crows-nest, registrations | E1 to E3 | 1 here, plus your console work | E3 | Each release |
| F Rollout | F1 to F4 | 4 | All four | One per release |
| G Docs | G1, G2 | 2 | PR review | Each release |

- **Critical path:** S1 → A3 → B2 → C1 → D1 → D4 → F1 and F3 (`v0.4.0`) → D2 → F2 (`v0.5.0`) → D3 → D5 → F4 (`v0.6.0`).
- **Parallel:** S2 to S4 and the E3 registrations happen while A to C are built.
- **Workflow:** unchanged. Claude Code never sees real provider credentials; tests use recorded fixtures and local fake servers.

Task format below: **ID. Name** · size · owner · depends on, then details and validation.

## Tasks

### S: Provider spikes

Phase S replaces assumptions about four outside systems with recorded evidence before any provider code is written. Each spike ends with a one-page note in `docs/spikes/` (go or no-go, plus any spec corrections) and synthetic fixtures in `tests/fixtures/<provider>/`, written from the observed response shapes, that the tests in phase D replay (raw captures from real accounts are never committed).

**S1. libical** · S · Claude · M3

- **Details:** add `libical` to the vcpkg manifest and cross-build it statically for `arm64-linux-rpi5`. Parse a fixture corpus (weekly and monthly rules, `EXDATE`, `RECURRENCE-ID` overrides, all-day events, floating times, events crossing the November DST change) and expand into the window. Record the binary size and RSS deltas and confirm the SBOM picks it up.
- **Validate:** go or no-go recorded; fixture corpus committed.

**S2. iCloud CalDAV** · S · Claude writes the probe, **you** run it · an app-specific password

- **Details:** a throwaway probe script on lovelace performs principal discovery at `caldav.icloud.com`, follows to the partition host, lists calendars and runs a `sync-collection` REPORT twice (initial, then after you edit one event). The password is typed at a prompt and never written to disk.
- **Validate:** note confirms the discovery chain, the partition host pattern and sync-token behavior; synthetic fixtures committed.

**S3. Google iCal feed** · S · Claude writes the probe, **you** run it · a secret iCal address

- **Details:** fetch one calendar's secret iCal address twice: before and after you create a recurring event with one moved instance and one private event. Record the response headers (`ETag`, `Last-Modified`, cache lifetime), the feed size, how exceptions and `CLASS` arrive, and how long a change takes to show up. The address is typed at a prompt and never written to disk.
- **Validate:** note confirms feed freshness and how recurrence, exceptions and privacy arrive; the address used for the spike is reset afterward.

**S4. Microsoft** · S · Claude writes the probe, **you** run it · E3 (Microsoft)

- **Details:** device flow on the `consumers` tenant; `calendarView` delta over a 455-day window; behavior of recurring instances, `sensitivity` and `showAs`; lifetime of a delta link.
- **Validate:** note confirms or corrects D3 and the window strategy; spike tokens revoked afterward.

### A: Domain and storage

Phase A builds the provider-independent core: what an occurrence is, how it is stored, and how recurring events become occurrences.

**A1. Calendar domain** · M · Claude · M3

- **Deliverables:** `app/modules/calendar/domain/`.
- **Details:** value types for owner, source (with the state machine), calendar, occurrence, `time_window` and `instance_key`; the privacy mapping as a pure function; a content hash over the stored fields. No dependencies beyond `core`.
- **Validate:** U-01 to U-05.

**A2. Schema and repositories** · M · Claude · A1

- **Deliverables:** `app/modules/calendar/migrations/0001_calendar.sql`; SQLite repositories in `adapters/sqlite/`.
- **Details:** the four tables from the data model; a range query that returns occurrences overlapping `[from, to)` using the two indexes; `remove_source` deletes calendars, occurrences and the source's token-store entries in one transaction.
- **Validate:** I-01 to I-03.

**A3. Recurrence adapter** · M · Claude · S1, A1

- **Deliverables:** `application` port `recurrence_expander`; libical adapter in `adapters/ical/`.
- **Details:** two entry points: whole iCalendar text (ICS and CalDAV) and a single event's RRULE, EXDATE and exception set (Google). Both return occurrences clipped to the window, honoring time zones, floating times and all-day rules from the spec.
- **Validate:** U-06 to U-11 (golden tests over the S1 fixture corpus).

**A4. Normalizer, diff and window slide** · M · Claude · A2, A3

- **Details:** applies the privacy mapping, computes per-calendar upserts and deletes by `(uid, instance_key)` and content hash, and commits them in one transaction. A nightly 03:00 job slides the window by triggering a full re-expansion per source.
- **Validate:** U-12 to U-14, I-04.

### B: Sync engine

Phase B runs every provider the same way, so a provider task in phase D only has to translate one external API.

**B1. Provider port and fake provider** · S · Claude · A1

- **Deliverables:** `application/ports/calendar_provider`; `tests/support/fake_provider`.
- **Details:** `list_calendars(source)` and `sync(source, calendar, state)` returning a change set (masters to expand or ready instances, deletions, new sync state, full-resync flag). Typed errors: `auth_revoked`, `transient`, `rate_limited(retry_after)`, `not_found`. The fake provider is scriptable per test.
- **Validate:** U-15.

**B2. Sync orchestrator** · M · Claude · A4, B1

- **Details:** one source sync = load its secrets, call the provider for each included calendar, normalize, diff, commit, then update state, status and metrics. Runs on a 2-worker pool, never on the database thread or a Drogon event loop. 5-minute ceiling per source sync.
- **Validate:** I-05 to I-07.

**B3. Scheduler** · M · Claude · B2

- **Details:** computes `next_run_at` with jitter, backoff and `Retry-After`; on `SIGHUP` re-reads sources and runs any with `sync_requested_at` set (how `source sync` works); on shutdown cancels in-flight syncs without committing partial results.
- **Validate:** U-16 to U-18, I-08.

**B4. Source states and sync metrics** · S · Claude · B3

- **Details:** the state machine from the spec; `needs_reconsent` and `paused` sources are never scheduled; staleness computed per source; all sync metrics exported.
- **Validate:** U-19, I-09.

### C: API and admin

Phase C is the contract the future SPA depends on, plus the only way you manage sources.

**C1. Calendar API** · M · Claude · A2

- **Deliverables:** handlers in `app/modules/calendar/adapters/http/`.
- **Details:** the three endpoints from the spec; range validation (dates parse, `from` before `to`, at most 62 days, inside the window); times rendered with the household time zone's offset; weak `ETag` from the latest `updated_at` among the matching rows; JSON errors; the bearer-token check (constant-time compare against the `api-token` credential) runs before routing. Queries run on the database thread.
- **Validate:** I-10 to I-14.

**C2. OpenAPI contract and tests** · S · Claude · C1

- **Deliverables:** `docs/api/openapi.yaml`; `tests/contract/` run in CI.
- **Details:** every response in I-10 to I-14 is validated against the schema. Uses `python3-jsonschema` from the pinned Debian snapshot, added to the toolchain image (you re-pin the digest).
- **Validate:** C-01.

**C3. Admin CLI and `class-t4ctl`** · M · Claude · B4

- **Deliverables:** CLI subcommands in `app/server/cli/`; `deploy/scripts/class-t4ctl`, shipped in the tarball and installed by `class-t4-deploy`.
- **Details:** every command and exit code in the spec. Secrets are read from the terminal without echo and go straight to the token store. `source add` for OAuth providers prints the verification URL and code and polls until consent, denial or the 15-minute timeout. The wrapper runs the CLI in a transient unit with both credentials, then sends `SIGHUP`.
- **Validate:** I-15 to I-17, T-23.

**C4. Egress extensions** · S · Claude · M3

- **Details:** the M3 `egress_policy` gains one-label wildcards (`*.icloud.com` matches `p42-caldav.icloud.com`, not `a.b.icloud.com`) and a second allowlist input supplied by modules through an egress-hosts hook (the calendar module returns its sources' hosts), read per request. Provider hosts move into `deploy/epiphany/class-t4.json`.
- **Validate:** U-20 to U-22.

### D: Providers

Phase D adds one provider per release. Each is tested against a local fake server that replays the synthetic fixtures from its spike, so CI never talks to a real provider.

**D1. ICS provider (`v0.4.0`)** · M · Claude · B4, C1, C3, C4

- **Details:** conditional GET through the M3 `http_client` (`If-None-Match`, `If-Modified-Since`); `webcal://` rewritten to `https://`; a 304 updates only `last_success_at`; a changed feed is parsed with A3 and replaces the source's occurrences. 401, 403 and 404 move the source to `needs_reconsent` (the feed URL or its token was revoked).
- **Validate:** I-18 to I-20.

**D2. CalDAV provider for iCloud (`v0.5.0`)** · L (3 sessions) · Claude · S2, D1

- **Details:** discovery chain from the S2 note (current-user-principal, calendar-home-set, calendars that support `VEVENT`), Basic auth with the stored app-specific password, `sync-collection` with the stored sync token, `calendar-multiget` for changed resources, ctag and ETag fallback if the S2 note calls for it. 401 moves the source to `needs_reconsent`.
- **Validate:** I-21 to I-24.

**D3. OAuth device flow and token lifecycle** · M · Claude · C3

- **Details:** a provider-neutral RFC 8628 client: device code request, display, polling that honors `interval`, `slow_down`, `authorization_pending`, `access_denied` and `expired_token`. Refresh tokens are replaced in the token store whenever the provider rotates them, in the same transaction as the source update. Access tokens are cached in memory until 60 seconds before expiry. `invalid_grant` becomes `auth_revoked`.
- **Validate:** U-23 to U-26, I-25.

**D4. Google through the ICS provider (`v0.4.0`)** · S · Claude · S3, D1

- **Details:** Google calendars are ICS sources that use the owner's secret iCal address. The ICS provider gains what the S3 note calls for: a body-hash comparison when the feed sends no `ETag`, and the `CLASS` mapping for private events. A Google API provider is deferred past v1.0, because Google's device flow does not allow calendar scopes and the desktop loopback flow needs an SSH tunnel for each consent.
- **Validate:** I-26 to I-28.

**D5. Microsoft provider (`v0.6.0`)** · M · Claude · S4, D3

- **Details:** `/me/calendars`; `calendarView` delta per calendar over the window, following `nextLink` pages and storing the `deltaLink`; instances mapped with `isAllDay`, start and end time zones, `sensitivity` and `showAs`; an expired delta link triggers a full resync; 429 honors `Retry-After`.
- **Validate:** I-29 to I-31.

### E: Contract, crows-nest and provider registrations

Phase E covers everything outside the class-t4 codebase that a provider release needs.

**E1. Ops contract per release** · S · Claude · each release

- **Details:** each release updates `docs/ops-contract.md`: API (pointing at `openapi.yaml`), new metrics, the `oauth-clients` credential, provider egress hosts, `class-t4ctl`, and the outbound requirements below.
- **Validate:** your review; the contract metric test stays green.

**E2. crows-nest outbound rules** · external · crows-nest · E1 for `v0.4.0`

- **Details:** no nftables change, since there is no output chain (D7); the `base` role ensures `ca-certificates` so the CA bundle path in the config exists.
- **Validate:** H-01.

**E3. Provider registrations** · **you** · before S4

- **Google:** no registration. Each owner copies the secret iCal address of each calendar (Calendar settings, Integrate calendar) when their source is added.
- **Microsoft:** Entra app registration for personal Microsoft accounts only, "Allow public client flows" set to Yes, delegated `Calendars.Read` and `offline_access`.
- **Apple:** each owner creates an app-specific password for their Apple ID when their source is added.
- **On epiphany:** write `/etc/class-t4/credentials/oauth-clients.json` (root 0600) with the Microsoft client ID, and `/etc/class-t4/credentials/api-token` (root 0600, 32 random bytes in base64).
- **Validate:** the S4 device flow completes with this registration.

### F: Rollout per release

Phase F is yours: each release goes out through the M2 path and then gets its real sources. Each step follows the runbook from G1, so the runbooks are tested by use.

**F1. `v0.4.0`: pipeline plus ICS** · S · **you** · D1, E1, E2

- **Details:** release and deploy; `class-t4ctl owner add` for each household member; add at least one ICS source (a school or holiday feed works); open `/api/v1/calendar/events` for the current month from a LAN client.
- **Validate:** H-01 to H-04.

**F2. `v0.5.0`: Apple** · S · **you** · D2, F1

- **Details:** deploy; add each Apple owner's source with their app-specific password; edit an event on an iPhone and wait for it to appear.
- **Validate:** H-05, H-06.

**F3. Google feeds (on `v0.4.0` or later)** · S · **you** · D4, F1

- **Details:** each Google owner pastes the secret iCal address of each calendar into `source add ics`; mark one event private; reset one secret address in Google, confirm `needs_reconsent`, then run `source reconsent` with the new address.
- **Validate:** H-07 to H-09.

**F4. `v0.6.0`: Microsoft and soak** · S · **you** · D5, F2

- **Details:** deploy; add the Microsoft source; create a recurring event with one moved instance; then leave everything running for 72 hours.
- **Validate:** H-10 to H-12.

### G: Docs

**G1. Runbooks** · S per release · Claude drafts, you review · each release

- **Deliverables:** `docs/runbooks/add-source-<provider>.md` (including what the owner does on their own device), `reconsent.md`, `sync-troubleshooting.md` (by source state and metric).
- **Validate:** each F task follows them as written.

**G2. ADRs and changelogs** · S · Claude · each release

- **Deliverables:** `docs/adr/0013-icalendar-library.md`, `0014-occurrence-window.md`, `0015-privacy-minimization.md`, `0016-admin-path.md`, `0017-egress-model.md`; a `CHANGELOG.md` section per release.
- **Validate:** your PR review.

## Test plan

72 tests prove M4: 26 unit, 32 integration, 1 deploy-tool test, 1 contract suite and 12 host tests. Every provider test replays synthetic fixtures against a local fake server, so CI is deterministic and never needs real accounts.

### Unit tests

| ID | Test | Task |
| --- | --- | --- |
| U-01 | Overlap uses half-open ranges; events spanning the whole range are included | A1 |
| U-02 | Privacy mapping: every row of the spec table | A1 |
| U-03 | Content hash is stable and changes when any stored field changes | A1 |
| U-04 | Source state machine allows only the specified transitions | A1 |
| U-05 | `instance_key` is identical for the same instance across syncs | A1 |
| U-06 | Weekly rule across the November DST change keeps local wall time | A3 |
| U-07 | Monthly by-day rule (second Tuesday) | A3 |
| U-08 | `EXDATE` removes an instance | A3 |
| U-09 | `RECURRENCE-ID` override moves an instance | A3 |
| U-10 | All-day end is exclusive; floating times use the household time zone | A3 |
| U-11 | An endless rule stops at the window edge | A3 |
| U-12 | Diff yields the right upserts and deletes | A4 |
| U-13 | Unchanged input yields an empty diff | A4 |
| U-14 | Window slide drops past instances and adds new ones | A4 |
| U-15 | Fake provider errors map to the typed errors | B1 |
| U-16 | Next-run jitter stays within plus or minus 10% | B3 |
| U-17 | Backoff doubles and caps at 6 hours | B3 |
| U-18 | `Retry-After` overrides backoff | B3 |
| U-19 | `needs_reconsent` and `paused` sources are never scheduled; stale after 3 intervals | B4 |
| U-20 | `*.icloud.com` matches one label only | C4 |
| U-21 | A source's host is allowed only while the source exists | C4 |
| U-22 | Provider hosts from config are allowed; everything else denied | C4 |
| U-23 | Device flow handles `slow_down` (interval plus 5 s) and `authorization_pending` | D3 |
| U-24 | `access_denied` exits 6; `expired_token` exits 7 | D3 |
| U-25 | Rotated refresh token replaces the stored one | D3 |
| U-26 | Access token reused until 60 s before expiry; `invalid_grant` becomes `auth_revoked` | D3 |

### Integration tests (`host-debug`)

| ID | Test | Task |
| --- | --- | --- |
| I-01 | Range query returns overlapping and spanning occurrences only | A2 |
| I-02 | `remove_source` deletes calendars, occurrences and token-store entries | A2 |
| I-03 | Calendar migrations pass the expand-only lint and apply on a v0.3.0 database | A2 |
| I-04 | A failure injected mid-commit leaves the previous occurrences intact | A4 |
| I-05 | Full sync through the fake provider populates occurrences | B2 |
| I-06 | Transient error keeps the last good data and sets `error` | B2 |
| I-07 | `auth_revoked` sets `needs_reconsent`; other sources keep syncing | B2 |
| I-08 | `SIGHUP` picks up a new source within 2 s; `sync_requested_at` runs it at once | B3 |
| I-09 | Sync metrics appear with the right labels after a sync | B4 |
| I-10 | Events for a range match the expected JSON | C1 |
| I-11 | Bad ranges (reversed, over 62 days, outside the window, malformed) return 400 with codes | C1 |
| I-12 | `owner` filter returns only that owner | C1 |
| I-13 | Repeat request with `If-None-Match` returns 304 | C1 |
| I-14 | `/sources` never contains URLs, hosts, accounts or secrets | C1 |
| I-15 | `owner add` and `owner list` | C3 |
| I-16 | `source add ics` reads the URL from a pty without echo and stores it only in the token store | C3 |
| I-17 | `source remove` leaves no rows or secrets behind | C3 |
| I-18 | ICS: 200 then 304 updates only `last_success_at` | D1 |
| I-19 | ICS: changed feed replaces the occurrences | D1 |
| I-20 | ICS: 401 and 404 set `needs_reconsent` | D1 |
| I-21 | CalDAV: discovery through the fake server reaches the partition host | D2 |
| I-22 | CalDAV: initial sync | D2 |
| I-23 | CalDAV: incremental sync fetches only changed resources | D2 |
| I-24 | CalDAV: 401 sets `needs_reconsent` | D2 |
| I-25 | Device flow end to end through the CLI and a fake OAuth server; token stored encrypted | D3 |
| I-26 | Google iCal fixture: a recurring event with one moved instance expands to the right occurrences | D4 |
| I-27 | Google feed without an `ETag`: an unchanged body updates only `last_success_at` | D4 |
| I-28 | Google feed: a `CLASS:PRIVATE` event becomes "Busy" | D4 |
| I-29 | Microsoft: initial delta with paging | D5 |
| I-30 | Microsoft: delta update; expired delta link forces a full resync | D5 |
| I-31 | Microsoft: `sensitivity` private becomes "Busy" | D5 |
| I-32 | A request with no token, or a wrong one, returns 401 on every /api/v1 endpoint; /health stays open | C1 |

### Other automated tests

| ID | Test | Task |
| --- | --- | --- |
| T-23 | `class-t4ctl` passes both credentials to the transient unit and sends `SIGHUP` (stubbed) | C3 |
| C-01 | Every API response in I-10 to I-14 validates against `openapi.yaml` | C2 |

### Host tests (you)

| ID | Test | Pass when | Task |
| --- | --- | --- | --- |
| H-01 | ICS live | Source `active`, occurrences present, events visible from a LAN client | F1 |
| H-02 | API limits | 90-day range returns 400; repeat with `ETag` returns 304 | F1 |
| H-03 | Restart and reboot | Syncing resumes; occurrence counts unchanged (no duplicates) | F1 |
| H-04 | Memory after 24 h | `MemoryCurrent` below `MemoryHigh` | F1 |
| H-05 | Apple edit | iPhone edit visible within one interval plus jitter | F2 |
| H-06 | Apple bad password | `needs_reconsent`; `source reconsent` fixes it | F2 |
| H-07 | Google feed | Owner pastes the secret iCal address; events present | F3 |
| H-08 | Google private event | Shows as "Busy" with no title or location | F3 |
| H-09 | Google address reset | Resetting the secret address puts the source in `needs_reconsent` within one interval; others unaffected; reconsent with the new address restores | F3 |
| H-10 | Microsoft recurrence | Moved instance appears at its new time only | F4 |
| H-11 | Restore with calendar data | M3 restore path works; canary and occurrences intact | F4 |
| H-12 | 72-hour soak | No stale source, every source has sync metrics, memory stable | F4 |

## Acceptance and sign-off

Each release is accepted on its own as its F task passes; M4 is done when `v0.6.0` passes the 72-hour soak and every box below is ticked. You are the approver.

- [ ] `v0.4.0` accepted (F1, H-01 to H-04)
- [ ] `v0.5.0` accepted (F2, H-05, H-06)
- [ ] Google feeds accepted (F3, H-07 to H-09)
- [ ] `v0.6.0` accepted (F4, H-10 to H-12)
- [ ] All 30 tasks closed (E2 closed in crows-nest); all 71 tests pass where they are meant to run
- [ ] `openapi.yaml`, ops contract, runbooks, ADRs 0013 to 0017 and per-release changelogs merged
- [ ] Spike notes S1 to S4 committed, with any spec corrections folded into this doc

## Risks

The biggest risks sit with the outside providers: their APIs, consent rules and rate limits decide the schedule more than the code does, which is why phase S comes first.

| Risk | Trigger | Response |
| --- | --- | --- |
| Provider behavior differs from the spec | A spike note contradicts a sync row | Fix the spec before the provider task starts; D-phase tasks wait on their spike |
| libical is too large or fragile when cross-built statically | S1 no-go | Fall back to a smaller parser for ICS and CalDAV and Google RRULE strings; ADR 0013 records it |
| Google's iCal feed lags or leaves data out | The S3 note shows stale or incomplete feeds | Build the deferred Google API provider with the desktop loopback flow over an SSH tunnel |
| iCloud changes its partition hosts or discovery | CalDAV 3xx to an unexpected host | One-label wildcard covers `pNN-caldav.icloud.com`; anything else fails closed and shows as `error` |
| Recurrence bugs show wrong times | Events off by an hour around DST | Golden tests U-06 to U-11 over the S1 corpus; add each real bug as a fixture |
| Memory grows with event volume | H-04 or H-12 near `MemoryHigh` | Window and minimized fields bound storage; M5 load test confirms |
| LAN trust exposes calendars to guests | Guests on the main Wi-Fi | The read token (D8); keep guests on a separate network where the router allows it |

## Open items

- [ ] D1 to D11 confirmed on 2026-11-12
- [ ] D9: are the Microsoft calendars on personal accounts, or work and school accounts?
- [ ] Household members and which provider each uses, so F1 to F4 can be planned per person
- [ ] Decided: D8 uses a read token from `v0.4.0`
- [ ] Household time zone: confirm `America/Los_Angeles`
- [ ] Window size: confirm 90 days back and 365 ahead

## Change log

| Date | Change |
| --- | --- |
| 2026-11-12 | Created from the project plan, the reference doc and the M3 doc |
| 2026-11-12 | Cross-repo review applied: Google moves to the ICS provider (D11) and M4 ends at v0.6.0; API read token (D8); calendar namespaces for metrics, CLI, config and secrets; module egress hook; synthetic fixtures only; no output chain |
