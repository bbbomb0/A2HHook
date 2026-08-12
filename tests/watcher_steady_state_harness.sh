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
NATIVE_COUNT="$WORK/native_count"
APPLY_COUNT="$WORK/apply_count"
PROBE_COUNT="$WORK/probe_count"
STATUS_COUNT="$WORK/status_count"
APPLIER="$WORK/a2h_apply"
export NATIVE_COUNT

printf '4242\n' > "$LAST_PID_FILE"
printf '7\n' > "$CFG_REVISION"
printf 'stable-snapshot\n' > "$APPLIED_SNAPSHOT"
printf '7\n' > "$APPLIED_REVISION"
printf '0\n' > "$NATIVE_COUNT"
printf '0\n' > "$APPLY_COUNT"
printf '0\n' > "$PROBE_COUNT"
printf '0\n' > "$STATUS_COUNT"

cat > "$APPLIER" <<'EOF'
#!/bin/sh
case "${1:-}" in
  snapshot-state)
    printf '%s\n' 'stable-snapshot|stable-raw'
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
  printf '%s\n' stable-raw
}

refresh_config_inotify() {
  rm -f "$CONFIG_EVENT_MARKER"
}

applier_busy() {
  return 1
}

set_runtime_status() {
  status_count=$(cat "$STATUS_COUNT")
  printf '%s\n' "$((status_count + 1))" > "$STATUS_COUNT"
}

find_hal_pid() {
  probe_count=$(cat "$PROBE_COUNT")
  printf '%s\n' "$((probe_count + 1))" > "$PROBE_COUNT"
  printf '%s\n' 4242
}

apply_once() {
  apply_count=$(cat "$APPLY_COUNT")
  printf '%s\n' "$((apply_count + 1))" > "$APPLY_COUNT"
}

log() {
  :
}

wait_for_watch_tick() {
  watch_elapsed_ticks=1
}

config_inotify_enabled=0
config_inotify_pid=
audio_watcher_pid=$$
watch_tick_seconds=2
watch_stable_ticks=3
watch_health_ticks=15

watcher=$(awk '
  /^last_pid=$/ { capture=1 }
  capture { print }
  capture && /^done$/ { exit }
' "$SERVICE")
[ -n "$watcher" ] || {
  printf 'FAIL: watcher loop not found\n' >&2
  exit 1
}

watcher=$(printf '%s\n' "$watcher" | sed \
  -e 's/^while true; do$/while [ "$watch_cycle" -lt 40 ]; do/')
eval "$watcher"

native_count=$(cat "$NATIVE_COUNT")
apply_count=$(cat "$APPLY_COUNT")
probe_count=$(cat "$PROBE_COUNT")
status_count=$(cat "$STATUS_COUNT")
[ "$watch_cycle" -eq 40 ] || {
  printf 'FAIL: watcher cycles=%s\n' "$watch_cycle" >&2
  exit 1
}
[ "$native_count" -eq 0 ] || {
  printf 'FAIL: steady watcher launched native inspection count=%s\n' "$native_count" >&2
  exit 1
}
[ "$apply_count" -eq 0 ] || {
  printf 'FAIL: steady watcher launched apply count=%s\n' "$apply_count" >&2
  exit 1
}
[ "$probe_count" -eq 2 ] || {
  printf 'FAIL: expected two lightweight PID probes, got %s\n' "$probe_count" >&2
  exit 1
}
[ "$status_count" -eq 2 ] || {
  printf 'FAIL: expected two steady status updates, got %s\n' "$status_count" >&2
  exit 1
}

printf 'WATCHER_STEADY_PASS cycles=%s probes=%s status=%s native=%s apply=%s\n' \
  "$watch_cycle" "$probe_count" "$status_count" "$native_count" "$apply_count"
