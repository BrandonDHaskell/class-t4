#!/usr/bin/env bash
# Whole-binary test for "class-t4 serve": both listeners, /health, /metrics,
# no static file exposure, clean SIGTERM shutdown, and CLI exit codes.
#
#   serve_test.sh <class-t4 binary> [emulator and its arguments...]
#
# Needs curl and openssl (both in the toolchain image).
set -euo pipefail

[[ $# -ge 1 ]] || { echo "usage: $0 <binary> [emulator...]" >&2; exit 2; }
binary=$1
shift
runner=("$@" "$binary")

readonly https_port=18443
readonly ops_port=19464
readonly startup_timeout=${CLASS_T4_STARTUP_TIMEOUT:-10}
readonly shutdown_timeout=10

work=$(mktemp -d)
server_pid=""
failures=0

cleanup() {
  if [[ -n $server_pid ]] && kill -0 "$server_pid" 2>/dev/null; then
    kill -KILL "$server_pid" 2>/dev/null || true
  fi
  rm -rf "$work"
}
trap cleanup EXIT

pass() { echo "ok: $1"; }
fail() {
  echo "FAIL: $1" >&2
  failures=$((failures + 1))
}
# expect <description> <expected> <actual>
expect() {
  if [[ $3 == "$2" ]]; then pass "$1"; else fail "$1: expected '$2', got '$3'"; fi
}
# expect_match <description> <regex> <actual>
expect_match() {
  if [[ $3 =~ $2 ]]; then pass "$1"; else fail "$1: '$3' does not match '$2'"; fi
}

# Throwaway TLS material, generated at runtime; nothing is committed.
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes \
  -days 1 -subj /CN=localhost -keyout "$work/key.pem" -out "$work/cert.pem" \
  2>/dev/null

cat >"$work/config.json" <<JSON
{
  "https": {"address": "127.0.0.1", "port": $https_port,
            "cert_file": "$work/cert.pem", "key_file": "$work/key.pem"},
  "ops": {"address": "127.0.0.1", "port": $ops_port},
  "threads": 2,
  "log_level": "info"
}
JSON

https_url="https://127.0.0.1:$https_port"
ops_url="http://127.0.0.1:$ops_port"
curl_opts=(--silent --show-error --max-time 5)

# --- CLI exit codes (no server needed) ---------------------------------------

version_line=$("${runner[@]}" --version)
expect_match "--version output" '^class-t4 [0-9]+\.[0-9]+\.[0-9]+ \(([0-9a-f]+|unknown)\)$' "$version_line"
version=$(sed -E 's/^class-t4 ([^ ]+) .*/\1/' <<<"$version_line")
commit=$(sed -E 's/^.*\((.*)\)$/\1/' <<<"$version_line")

status=0
"${runner[@]}" bogus >/dev/null 2>&1 || status=$?
expect "unknown command exits 2" 2 "$status"

status=0
"${runner[@]}" serve --config "$work/missing.json" >/dev/null 2>&1 || status=$?
expect "missing config exits 1" 1 "$status"

echo '{"https": {"cert_file": "/c", "key_file": "/k", "prot": 1}}' >"$work/unknown-key.json"
status=0
"${runner[@]}" serve --config "$work/unknown-key.json" >/dev/null 2>&1 || status=$?
expect "unknown config key exits 1" 1 "$status"

echo "{\"https\": {\"cert_file\": \"$work/none.pem\", \"key_file\": \"$work/key.pem\"}}" >"$work/no-cert.json"
status=0
"${runner[@]}" serve --config "$work/no-cert.json" >/dev/null 2>&1 || status=$?
expect "unreadable certificate exits 1" 1 "$status"

# --- serve -------------------------------------------------------------------

# Run from a directory with files in it, to prove none of them are served.
mkdir "$work/cwd"
echo "secret" >"$work/cwd/probe.txt"
echo "secret" >"$work/cwd/probe"

(cd "$work/cwd" && exec "${runner[@]}" serve --config "$work/config.json") \
  >"$work/server.log" 2>&1 &
server_pid=$!

deadline=$((SECONDS + startup_timeout))
until curl "${curl_opts[@]}" --fail -o /dev/null "$ops_url/health" 2>/dev/null; do
  if ! kill -0 "$server_pid" 2>/dev/null; then
    echo "server exited during startup; log:" >&2
    cat "$work/server.log" >&2
    exit 1
  fi
  if ((SECONDS >= deadline)); then
    echo "server not ready after ${startup_timeout}s; log:" >&2
    cat "$work/server.log" >&2
    exit 1
  fi
  sleep 0.2
done
pass "server ready"

expected_health="{\"status\":\"ok\",\"version\":\"$version\"}"

body=$(curl "${curl_opts[@]}" --insecure -w '\n%{http_code} %{content_type}' "$https_url/health")
expect "https /health body" "$expected_health" "$(head -1 <<<"$body")"
expect_match "https /health status and type" '^200 application/json' "$(tail -1 <<<"$body")"

body=$(curl "${curl_opts[@]}" -w '\n%{http_code} %{content_type}' "$ops_url/health")
expect "ops /health body" "$expected_health" "$(head -1 <<<"$body")"
expect_match "ops /health status and type" '^200 application/json' "$(tail -1 <<<"$body")"

metrics=$(curl "${curl_opts[@]}" -o "$work/metrics" -w '%{http_code} %{content_type}' "$ops_url/metrics")
expect "ops /metrics status and type" "200 text/plain; version=0.0.4; charset=utf-8" "$metrics"
expect "metrics TYPE line" "1" "$(grep -cx '# TYPE class_t4_build_info gauge' "$work/metrics")"
expect "metrics build_info sample" "1" \
  "$(grep -cxF "class_t4_build_info{version=\"$version\",commit=\"$commit\"} 1" "$work/metrics")"

expect "https /metrics is 404" 404 \
  "$(curl "${curl_opts[@]}" --insecure -o /dev/null -w '%{http_code}' "$https_url/metrics")"
expect "POST /health is 405" 405 \
  "$(curl "${curl_opts[@]}" -o /dev/null -X POST -w '%{http_code}' "$ops_url/health")"

for path in /probe.txt /probe; do
  expect "ops $path not served" 404 \
    "$(curl "${curl_opts[@]}" -o /dev/null -w '%{http_code}' "$ops_url$path")"
  expect "https $path not served" 404 \
    "$(curl "${curl_opts[@]}" --insecure -o /dev/null -w '%{http_code}' "$https_url$path")"
done

expect "no Server header" 0 \
  "$(curl "${curl_opts[@]}" -o /dev/null -D - "$ops_url/health" | grep -ci '^server:' || true)"

# --- shutdown ----------------------------------------------------------------

kill -TERM "$server_pid"
deadline=$((SECONDS + shutdown_timeout))
while kill -0 "$server_pid" 2>/dev/null && ((SECONDS < deadline)); do
  sleep 0.2
done
if kill -0 "$server_pid" 2>/dev/null; then
  fail "still running ${shutdown_timeout}s after SIGTERM"
else
  status=0
  wait "$server_pid" || status=$?
  expect "exit status after SIGTERM" 0 "$status"
fi
server_pid=""

if ((failures > 0)); then
  echo "--- server log" >&2
  cat "$work/server.log" >&2
  echo "$failures check(s) failed" >&2
  exit 1
fi
echo "all checks passed"
