#!/bin/sh
set -eu

SERVICE=${1:-service.sh}
[ -f "$SERVICE" ] || {
  printf 'FAIL: service script not found: %s\n' "$SERVICE" >&2
  exit 1
}

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM HUP

LAST_PID_FILE="$WORK/last_pid"
CFG_REVISION="$WORK/revision"
APPLIED_SNAPSHOT="$WORK/applied_snapshot"
APPLIED_REVISION="$WORK/applied_revision"
CONFIG_EVENT_MARKER="$WORK/config.changed"
RAW_STATE="$WORK/raw_state"
RAW_CALLS="$WORK/raw_calls"
APPLY_COUNT="$WORK/apply_count"
NATIVE_COUNT="$WORK/native_count"
PROBE_COUNT="$WORK/probe_count"
APPLIER="$WORK/a2h_apply"
export CFG_REVISION RAW_STATE APPLY_COUNT NATIVE_COUNT

printf '4242\n' > "$LAST_PID_FILE"
printf '7\n' > "$CFG_REVISION"
printf 'stable-snapshot\n' > "$APPLIED_SNAPSHOT"
printf '7\n' > "$APPLIED_REVISION"
printf 'stable-raw\n' > "$RAW_STATE"
printf '0\n' > "$RAW_CALLS"
printf '0\n' > "$APPLY_COUNT"
printf '0\n' > "$NATIVE_COUNT"
printf '0\n' > "$PROBE_COUNT"
: > "$CONFIG_EVENT_MARKER"

cat > "$APPLIER" <<'EOF'
#!/bin/sh
case "${1:-}" in
  snapshot-state)
    printf '8\n' > "$CFG_REVISION"
    printf '%s\n' 'changed-snapshot|changed-raw'
    ;;
  check|status|show)
    native_count=$(cat "$NATIVE_COUNT")
    printf '%s\n' "$((native_count + 1))" > "$NATIVE_COUNT"
    ;;
  *)
    exit 97
    ;;
esac
EOF
chmod 0755 "$APPLIER"

raw_config_signature() {
  raw_calls=$(cat "$RAW_CALLS")
  raw_calls=$((raw_calls + 1))
  printf '%s\n' "$raw_calls" > "$RAW_CALLS"
  cat "$RAW_STATE"
  if [ "$raw_calls" -eq 2 ]; then
    # The replacement happens after the event loop sampled the old signature,
    # while the restarted inotifyd process is not yet ready to report it.
    printf 'changed-raw\n' > "$RAW_STATE"
  fi
}

refresh_config_inotify() {
  rm -f "$CONFIG_EVENT_MARKER"
  config_inotify_enabled=1
  config_inotify_pid=$$
}

applier_busy() {
  return 1
}

set_runtime_status() {
  :
}

find_hal_pid() {
  probe_count=$(cat "$PROBE_COUNT")
  printf '%s\n' "$((probe_count + 1))" > "$PROBE_COUNT"
  printf '%s\n' 4242
}

apply_once() {
  apply_count=$(cat "$APPLY_COUNT")
  printf '%s\n' "$((apply_count + 1))" > "$APPLY_COUNT"
  printf 'changed-snapshot\n' > "$APPLIED_SNAPSHOT"
  printf '8\n' > "$APPLIED_REVISION"
}

log() {
  :
}

config_inotify_enabled=1
config_inotify_pid=$$

watcher=$(awk '
  /^last_pid=\$\(cat / { capture=1 }
  capture { print }
  capture && /^done$/ { exit }
' "$SERVICE")
[ -n "$watcher" ] || {
  printf 'FAIL: watcher loop not found\n' >&2
  exit 1
}

watcher=$(printf '%s\n' "$watcher" | sed \
  -e 's/^while true; do$/while [ "$watch_cycle" -lt 6 ]; do/' \
  -e 's/^  sleep "$watch_tick_seconds"$/  :/')
eval "$watcher"

apply_count=$(cat "$APPLY_COUNT")
native_count=$(cat "$NATIVE_COUNT")
probe_count=$(cat "$PROBE_COUNT")
raw_calls=$(cat "$RAW_CALLS")
[ "$apply_count" -eq 1 ] || {
  printf 'FAIL: rearm-gap edit apply count=%s\n' "$apply_count" >&2
  exit 1
}
[ "$native_count" -eq 0 ] || {
  printf 'FAIL: rearm-gap path launched native inspection count=%s\n' "$native_count" >&2
  exit 1
}
[ "$probe_count" -eq 1 ] || {
  printf 'FAIL: rearm-gap lightweight probe count=%s\n' "$probe_count" >&2
  exit 1
}
[ "$raw_calls" -ge 4 ] || {
  printf 'FAIL: rearm-gap grace polling did not span debounce calls=%s\n' "$raw_calls" >&2
  exit 1
}

printf 'WATCHER_REARM_PASS cycles=%s raw_calls=%s probes=%s native=%s apply=%s\n' \
  "$watch_cycle" "$raw_calls" "$probe_count" "$native_count" "$apply_count"
