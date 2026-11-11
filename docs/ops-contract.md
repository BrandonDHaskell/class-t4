# Ops contract

What class-t4 exposes to the host and to monitoring. Any change to a port, path or endpoint updates this file in the same commit.

Status: M1 draft. The service user, data directory and backup command are added at the end of M1.

## Listeners

| Listener | Default bind | Protocol | Purpose |
| --- | --- | --- | --- |
| API | `0.0.0.0:8443` | HTTPS (TLS, cert and key from config) | `/api/v1/...` and `/health` |
| Ops | `0.0.0.0:9464` | Plain HTTP | `/metrics` and `/health`, for monitoring on tranquility |

Both are LAN-only by host firewall policy; the process itself binds to the configured addresses.

## Endpoints

| Path | Listener | Method | Response |
| --- | --- | --- | --- |
| `/health` | API and ops | GET | `200`, `application/json`, `{"status":"ok","version":"<semver>"}` |
| `/metrics` | Ops only | GET | `200`, `text/plain; version=0.0.4; charset=utf-8`, Prometheus text format |
| `/metrics` | API | GET | `404` (metrics are never served on the API listener) |

Other methods on these paths return `405`. Every other path returns `404`: no static files are served, and the working directory is never read.

### Metrics

| Metric | Type | Labels | Meaning |
| --- | --- | --- | --- |
| `class_t4_build_info` | gauge | `version`, `commit` | Always `1`; identifies the running build |

## Command line

| Command | Exit codes |
| --- | --- |
| `class-t4 serve --config <path>` | `0` after a clean shutdown on SIGTERM or SIGINT, `1` on a config or startup error, `2` on bad usage |
| `class-t4 --version` | `0`; prints `class-t4 <version> (<commit>)` |
| `class-t4 --help` | `0` |

Logs go to stdout and stderr (journald under systemd).

## Filesystem

| Path | Access | Purpose |
| --- | --- | --- |
| Config file (`--config`) | read | Loaded once at startup |
| `https.cert_file`, `https.key_file` | read | Checked before listening |
| `$TMPDIR/class-t4/` (default `/tmp/class-t4/`) | write | Empty document root and upload scratch that Drogon requires; nothing is stored. Private to the service under `PrivateTmp=yes` |

## Config

A single JSON file, path given by `serve --config`. Example: `config/class-t4.example.json`. Unknown keys are rejected at startup.

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| `https.address` | string | `0.0.0.0` | IPv4 or IPv6 literal for the API listener |
| `https.port` | integer | `8443` | API listener port |
| `https.cert_file` | string | required | PEM certificate chain for the API listener |
| `https.key_file` | string | required | PEM private key for the API listener |
| `ops.address` | string | `0.0.0.0` | IPv4 or IPv6 literal for the ops listener |
| `ops.port` | integer | `9464` | Ops listener port; must differ from `https.port` |
| `threads` | integer | `2` | HTTP worker threads, 1 to 64 |
| `log_level` | string | `info` | One of `trace`, `debug`, `info`, `warn`, `error` |

Planned on-host paths (installed in M2): config at `/etc/class-t4/class-t4.json`, TLS files under `/etc/class-t4/tls/`. The TLS certificate source is an open item in `docs/design.md`.
