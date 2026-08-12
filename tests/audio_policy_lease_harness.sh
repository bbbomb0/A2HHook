#!/bin/sh

set -u

WATCHER=${1:-}
[ -f "$WATCHER" ] || {
  echo "FAIL watcher-missing"
  exit 1
}

if [ -n "${A2H_TEST_BASE:-}" ]; then
  BASE="$A2H_TEST_BASE/a2h_policy_lease_$$"
  mkdir "$BASE" || exit 1
else
  BASE=$(mktemp -d "${TMPDIR:-/tmp}/a2h-policy-lease.XXXXXX") || exit 1
fi
MODULE="$BASE/module"
CONFIG="$MODULE/config"
BIN="$MODULE/bin"
RUNTIME="$BASE/runtime"
EVENTS="$BASE/events"
SU_LOG="$BASE/su.log"
TRIGGER_LOG="$BASE/trigger.log"
WATCH_LOG="$BASE/watch.log"
WATCH_PID=

cleanup() {
  exec 5>&- 2>/dev/null || true
  if [ -n "$WATCH_PID" ]; then
    kill "$WATCH_PID" 2>/dev/null || true
    wait "$WATCH_PID" 2>/dev/null || true
  fi
  rm -rf "$BASE"
}
trap cleanup EXIT INT TERM HUP

mkdir -p "$CONFIG" "$BIN" || exit 1
printf 'enabled\n' > "$CONFIG/state"
: > "$CONFIG/packages.txt"
: > "$CONFIG/package_states"
printf 'com.example.game 10420 0 /data/user/0/com.example.game\n' > "$BASE/packages.list"

cat > "$BIN/a2h_trigger" <<'EOF'
#!/bin/sh
[ "${1:-}" = --lease ] || exit 2
token=$2
session=$3
printf '9001\n' > "$session" || exit 1
sleep 0.02
printf '9001 ready\n' > "$session" || exit 1
printf 'start\n' >> "$A2H_TEST_TRIGGER_LOG"
while [ -f "$token" ]; do sleep 0.02; done
printf 'stop\n' >> "$A2H_TEST_TRIGGER_LOG"
exit 0
EOF
chmod 0755 "$BIN/a2h_trigger"

cat > "$BASE/fake-su" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$A2H_TEST_SU_LOG"
[ "$#" -eq 3 ] && [ "$2" = -c ] || exit 2
exec "${A2H_TEST_SHELL:-/system/bin/sh}" -c "$3"
EOF
chmod 0755 "$BASE/fake-su"
mkfifo "$EVENTS" || exit 1

wait_for() {
  wait_attempt=0
  while [ "$wait_attempt" -lt 200 ]; do
    "$@" && return 0
    sleep 0.02
    wait_attempt=$((wait_attempt + 1))
  done
  return 1
}

file_equals() {
  expected_path=$1
  expected_value=$2
  actual_value=
  [ -r "$expected_path" ] || return 1
  IFS= read -r actual_value < "$expected_path" || true
  [ "$actual_value" = "$expected_value" ]
}

directory_empty() {
  empty_dir=$1
  for empty_item in "$empty_dir"/*; do
    [ ! -e "$empty_item" ] || return 1
  done
  return 0
}

session_equals() {
  session_dir=$1
  session_expected=$2
  for session_candidate in "$session_dir"/*; do
    [ -f "$session_candidate" ] || continue
    session_actual=
    session_state=
    IFS=' ' read -r session_actual session_state < "$session_candidate" || true
    [ "$session_actual" = "$session_expected" ] &&
      [ "$session_state" = ready ] && return 0
  done
  return 1
}

A2H_MODDIR="$MODULE" \
A2H_CONFIG_DIR="$CONFIG" \
A2H_PACKAGES_LIST="$BASE/packages.list" \
A2H_RUNTIME_DIR="$RUNTIME" \
A2H_LOG_FILE="$WATCH_LOG" \
A2H_SU_BIN="$BASE/fake-su" \
A2H_SKIP_CHOWN=1 \
A2H_COOLDOWN_SECONDS=0 \
A2H_TEST_SU_LOG="$SU_LOG" \
A2H_TEST_TRIGGER_LOG="$TRIGGER_LOG" \
  "${A2H_TEST_SHELL:-/system/bin/sh}" "$WATCHER" --file "$EVENTS" &
WATCH_PID=$!
exec 5> "$EVENTS"

mkdir -p "$RUNTIME/sessions/com.example.game"
stale_session="$RUNTIME/sessions/com.example.game/stale"
printf '7777 ready\n' > "$stale_session"
printf '%s\n' 'TransferEvent event = {"name":"audio_track_message","audio_event":{"app_name":"com.example.game", "scenario":"playback"}}' >&5
wait_for file_equals "$RUNTIME/leases/com.example.game" fallback:70 || {
  echo "FAIL fallback-lease"
  exit 1
}
[ ! -f "$stale_session" ] || {
  echo "FAIL stale-session-not-cleared"
  exit 1
}
wait_for session_equals "$RUNTIME/sessions/com.example.game" 9001 || {
  echo "FAIL trigger-session"
  exit 1
}

# The trigger stream's own AudioPolicy start must not recurse or create a port.
printf '%s\n' 'D/APM_AudioPolicyManager: startOutput() output 13, portId 90, stream 3, session 9001, sample_rate 48000, appname com.example.game)' >&5
sleep 0.05
[ ! -e "$RUNTIME/ports/90" ] || {
  echo "FAIL self-session-port"
  exit 1
}

# Historical dumpsys ordering and current live ordering must both parse.
printf '%s\n' 'audioPlayDump: startOutput() output 29, stream 3, session 101, portId 11, appname com.example.game uid 10420' >&5
wait_for file_equals "$RUNTIME/leases/com.example.game" policy || {
  echo "FAIL policy-promotion"
  exit 1
}
wait_for file_equals "$RUNTIME/ports/11" 'com.example.game 101' || {
  echo "FAIL historical-start"
  exit 1
}
printf '%s\n' 'D/APM_AudioPolicyManager: startOutput() output 13, portId 12, stream 3, session 102, sample_rate 48000, appname com.example.game)' >&5
wait_for file_equals "$RUNTIME/ports/12" 'com.example.game 102' || {
  echo "FAIL live-start"
  exit 1
}

# A stale stop cannot remove a reused port; one remaining real port keeps policy.
printf '%s\n' 'audioPlayDump: stopOutput() output 29, stream 3, session 999 portId 11 [uid=10420]' >&5
sleep 0.05
[ -f "$RUNTIME/ports/11" ] || {
  echo "FAIL stale-stop-removed-port"
  exit 1
}
printf '%s\n' 'audioPlayDump: stopOutput() output 29, stream 3, session 101 portId 11 [uid=10420]' >&5
wait_for test ! -e "$RUNTIME/ports/11" || {
  echo "FAIL historical-stop"
  exit 1
}
file_equals "$RUNTIME/leases/com.example.game" policy || {
  echo "FAIL premature-policy-release"
  exit 1
}

printf '%s\n' 'D/APM_AudioPolicyManager: stoptOutput() output 13, portId 12, stream 3, session 102, sample_rate 48000, appname com.example.game)' >&5
wait_for test ! -e "$RUNTIME/leases/com.example.game" || {
  echo "FAIL live-stop"
  exit 1
}
exec 5>&-
wait "$WATCH_PID"
WATCH_RC=$?
WATCH_PID=

[ "$WATCH_RC" -eq 0 ] || {
  echo "FAIL watcher-rc-$WATCH_RC"
  exit 1
}
[ "$(wc -l < "$SU_LOG")" -eq 1 ] || {
  echo "FAIL recursive-trigger"
  exit 1
}
[ "$(sed -n '1p' "$TRIGGER_LOG")" = start ] &&
[ "$(sed -n '2p' "$TRIGGER_LOG")" = stop ] || {
  echo "FAIL trigger-lifecycle"
  exit 1
}
directory_empty "$RUNTIME/ports" &&
directory_empty "$RUNTIME/leases" &&
directory_empty "$RUNTIME/lease_pids" &&
directory_empty "$RUNTIME/sessions" || {
  echo "FAIL runtime-cleanup"
  exit 1
}

echo "PASS audio policy lease lifecycle"
