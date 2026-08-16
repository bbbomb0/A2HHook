#!/system/bin/sh

WATCHER=${1:-}
[ -n "$WATCHER" ] && [ -f "$WATCHER" ] || exit 2

BASE=${A2H_TEST_BASE:-/data/local/tmp}/a2h_uid_watcher_$$
MODULE="$BASE/module"
CONFIG="$MODULE/config"
BIN="$MODULE/bin"
PACKAGES_LIST="$BASE/packages.list"
SU_LOG="$BASE/su.log"
WATCH_LOG="$BASE/watch.log"
FAKE_SU="$BASE/fake-su"

fail() {
  printf 'FAIL %s\n' "$1"
  [ ! -f "$WATCH_LOG" ] || { printf '%s\n' '--- watcher log ---'; tail -40 "$WATCH_LOG"; }
  [ ! -f "$SU_LOG" ] || { printf '%s\n' '--- su log ---'; tail -20 "$SU_LOG"; }
  exit 1
}

cleanup() {
  if [ "${A2H_TEST_KEEP:-0}" = 1 ]; then
    printf 'KEPT %s\n' "$BASE"
    return 0
  fi
  rm -rf "$BASE"
}
trap cleanup EXIT INT TERM HUP

mkdir -p "$CONFIG" "$BIN" || fail mkdir
cat > "$BIN/a2h_trigger" <<'EOF'
#!/system/bin/sh
exit 0
EOF
chmod 0755 "$BIN/a2h_trigger" || fail trigger-mode

cat > "$FAKE_SU" <<'EOF'
#!/system/bin/sh
printf '%s\n' "$*" >> "$A2H_TEST_SU_LOG"
set -- $3
if [ "${A2H_TEST_SU_RC:-0}" -eq 0 ]; then
  printf '4242 ready\n' > "$4"
  while [ -f "$3" ]; do
    sleep 0.05
  done
  exit 0
fi
sleep 0.05
exit "$A2H_TEST_SU_RC"
EOF
chmod 0755 "$FAKE_SU" || fail fake-su-mode

LONG_PACKAGE=com.example.xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
cat > "$PACKAGES_LIST" <<EOF
com.example.globalgame 10420 0 /data/user/0/com.example.globalgame
org.sample.rhythm 10421 0 /data/user/0/org.sample.rhythm
com.android.systemui 1000 0 /data/user/0/com.android.systemui
$LONG_PACKAGE 10422 0 /data/user/0/$LONG_PACKAGE
EOF

write_slot_config() {
  slot_mode=$1
  slot_enabled=$2
  printf '%s\n' "$slot_mode" > "$CONFIG/state" || return 1
  cat > "$CONFIG/packages.txt" <<'EOF'






org.sample.rhythm



EOF
  cat > "$CONFIG/package_states" <<EOF
0
0
0
0
0
0
$slot_enabled
0
0
0
EOF
}

run_case() {
  case_name=$1
  case_mode=$2
  case_enabled=$3
  case_su_rc=$4
  case_cooldown=$5
  case_events="$BASE/$case_name.events"
  case_runtime="$BASE/runtime-$case_name"
  write_slot_config "$case_mode" "$case_enabled" || return 1
  : > "$SU_LOG"
  : > "$WATCH_LOG"
  A2H_MODDIR="$MODULE" \
  A2H_CONFIG_DIR="$CONFIG" \
  A2H_PACKAGES_LIST="$PACKAGES_LIST" \
  A2H_TRIGGER_SOURCE="$BIN/a2h_trigger" \
  A2H_RUNTIME_DIR="$case_runtime" \
  A2H_LOG_FILE="$WATCH_LOG" \
  A2H_SU_BIN="$FAKE_SU" \
  A2H_SKIP_CHOWN=1 \
  A2H_COOLDOWN_SECONDS="$case_cooldown" \
  A2H_TEST_SU_LOG="$SU_LOG" \
  A2H_TEST_SU_RC="$case_su_rc" \
    /system/bin/sh "$WATCHER" --file "$case_events"
}

cat > "$BASE/global.events" <<'EOF'
TransferEvent event = {"name":"audio_track_message","audio_event":{"app_name":"com.example.globalgame", "scenario":"playback"}}
TransferEvent event = {"name":"audio_track_message","audio_event":{"app_name":"com.example.globalgame", "scenario":"playback"}}
EOF
run_case global enabled 0 0 30 || fail global-rc
[ "$(wc -l < "$SU_LOG" | tr -d ' ')" = 1 ] || fail global-count
grep -F -q '10420 -c ' "$SU_LOG" || fail global-uid

cat > "$BASE/custom.events" <<'EOF'
TransferEvent event = {"name":"audio_track_message","audio_event":{"app_name":"org.sample.rhythm", "scenario":"playback"}}
EOF
run_case custom disabled 1 0 30 || fail custom-rc
[ "$(wc -l < "$SU_LOG" | tr -d ' ')" = 1 ] || fail custom-count
grep -F -q '10421 -c ' "$SU_LOG" || fail custom-uid

cp "$BASE/custom.events" "$BASE/disabled.events" || fail disabled-events
run_case disabled disabled 0 0 30 || fail disabled-rc
[ ! -s "$SU_LOG" ] || fail disabled-triggered

cat > "$BASE/rejected.events" <<'EOF'
TransferEvent event = {"name":"audio_track_message","audio_event":{"app_name":"bad/package", "scenario":"playback"}}
TransferEvent event = {"name":"audio_track_message","audio_event":{"app_name":"", "scenario":"playback"}}
TransferEvent event = {"name":"audio_track_message","audio_event":{"app_name":"com.example.globalgame", "scenario":"capture"}}
TransferEvent event = {"name":"music_playback","audio_event":{"app_name":"com.example.globalgame", "scenario":"playback"}}
TransferEvent event = {"name":"audio_track_message","audio_event":{"app_name":"com.android.systemui", "scenario":"playback"}}
TransferEvent event = {"name":"audio_track_message","audio_event":{"app_name":"com.android.systemui", "scenario":"playback"}}
TransferEvent event = {"name":"audio_track_message","audio_event":{"app_name":"net.missing.game", "scenario":"playback"}}
EOF
run_case rejected enabled 0 0 0 || fail rejected-rc
[ ! -s "$SU_LOG" ] || fail rejected-triggered
[ "$(grep -c 'reason=core-system-uid' "$WATCH_LOG")" = 1 ] || fail core-cache

cat > "$BASE/failure.events" <<'EOF'
TransferEvent event = {"name":"audio_track_message","audio_event":{"app_name":"com.example.globalgame", "scenario":"playback"}}
EOF
run_case failure enabled 0 17 30 || fail failure-rc
[ "$(wc -l < "$SU_LOG" | tr -d ' ')" = 1 ] || fail failure-count
grep -F -q 'lease trigger FAIL package=com.example.globalgame uid=10420 rc=17' "$WATCH_LOG" ||
  fail failure-log

cat > "$BASE/spaced.events" <<'EOF'
TransferEvent event = {"name" : "audio_track_message","audio_event" : {"app_name" : "com.example.globalgame", "scenario" : "playback"}}
EOF
run_case spaced enabled 0 0 30 || fail spaced-rc
[ "$(wc -l < "$SU_LOG" | tr -d ' ')" = 1 ] || fail spaced-count

cat > "$BASE/long.events" <<EOF
TransferEvent event = {"name":"audio_track_message","audio_event":{"app_name":"$LONG_PACKAGE", "scenario":"playback"}}
EOF
run_case long enabled 0 0 30 || fail long-rc
[ "$(wc -l < "$SU_LOG" | tr -d ' ')" = 1 ] || fail long-count
grep -F -q '10422 -c ' "$SU_LOG" || fail long-uid

wait_for_su_count() {
  expected_count=$1
  count_try=0
  while [ "$count_try" -lt 100 ]; do
    current_count=$(wc -l < "$SU_LOG" | tr -d ' ')
    [ "$current_count" = "$expected_count" ] && return 0
    sleep 0.05
    count_try=$((count_try + 1))
  done
  return 1
}

wait_for_path() {
  expected_path=$1
  expected_state=$2
  path_try=0
  while [ "$path_try" -lt 100 ]; do
    if [ "$expected_state" = present ] && [ -e "$expected_path" ]; then
      return 0
    fi
    if [ "$expected_state" = absent ] && [ ! -e "$expected_path" ]; then
      return 0
    fi
    sleep 0.05
    path_try=$((path_try + 1))
  done
  return 1
}

reconcile_runtime="$BASE/runtime-reconcile"
reconcile_fifo="$BASE/reconcile.events"
write_slot_config disabled 0 || fail reconcile-config
: > "$SU_LOG"
: > "$WATCH_LOG"
mkfifo "$reconcile_fifo" || fail reconcile-fifo
A2H_MODDIR="$MODULE" \
A2H_CONFIG_DIR="$CONFIG" \
A2H_PACKAGES_LIST="$PACKAGES_LIST" \
A2H_TRIGGER_SOURCE="$BIN/a2h_trigger" \
A2H_RUNTIME_DIR="$reconcile_runtime" \
A2H_LOG_FILE="$WATCH_LOG" \
A2H_SU_BIN="$FAKE_SU" \
A2H_SKIP_CHOWN=1 \
A2H_COOLDOWN_SECONDS=0 \
A2H_TEST_SU_LOG="$SU_LOG" \
A2H_TEST_SU_RC=0 \
  /system/bin/sh "$WATCHER" --file "$reconcile_fifo" &
reconcile_watcher=$!
exec 9> "$reconcile_fifo"

printf '%s\n' 'startOutput() session 7001, portId 6001, appname org.sample.rhythm uid 10421' >&9
wait_for_path "$reconcile_runtime/ports/6001" present || fail reconcile-denied-port
[ ! -s "$SU_LOG" ] || fail reconcile-denied-trigger

printf '%s\n' enabled > "$CONFIG/state" || fail reconcile-global-state
printf '%s\n' global > "$reconcile_runtime/reconcile" || fail reconcile-global-marker
printf '%s\n' '__A2H_RECONCILE__' >&9
wait_for_su_count 1 || fail reconcile-global-start
wait_for_path "$reconcile_runtime/leases/org.sample.rhythm" present || fail reconcile-global-token

write_slot_config disabled 0 || fail reconcile-disabled-state
printf '%s\n' disabled > "$reconcile_runtime/reconcile" || fail reconcile-disabled-marker
printf '%s\n' '__A2H_RECONCILE__' >&9
wait_for_path "$reconcile_runtime/leases/org.sample.rhythm" absent || fail reconcile-disabled-stop
[ "$(wc -l < "$SU_LOG" | tr -d ' ')" = 1 ] || fail reconcile-disabled-restart

write_slot_config disabled 1 || fail reconcile-custom-state
printf '%s\n' custom > "$reconcile_runtime/reconcile" || fail reconcile-custom-marker
printf '%s\n' '__A2H_RECONCILE__' >&9
wait_for_su_count 2 || fail reconcile-custom-start

printf '%s\n' 'startOutput() portId 6002, session 7002, appname org.sample.rhythm uid 10421' >&9
wait_for_path "$reconcile_runtime/ports/6002" present || fail reconcile-second-port
[ "$(wc -l < "$SU_LOG" | tr -d ' ')" = 2 ] || fail reconcile-second-port-duplicate
printf '%s\n' multi > "$reconcile_runtime/reconcile" || fail reconcile-multi-marker
printf '%s\n' '__A2H_RECONCILE__' >&9
wait_for_su_count 3 || fail reconcile-multi-once

printf '%s\n' 'stopOutput() session 7001 portId 6001 uid=10421' >&9
wait_for_path "$reconcile_runtime/ports/6001" absent || fail reconcile-first-stop
wait_for_path "$reconcile_runtime/leases/org.sample.rhythm" present || fail reconcile-first-stop-token
printf '%s\n' 'stoptOutput() portId 6002 session 7002 uid=10421' >&9
wait_for_path "$reconcile_runtime/ports/6002" absent || fail reconcile-second-stop
wait_for_path "$reconcile_runtime/leases/org.sample.rhythm" absent || fail reconcile-final-stop-token

exec 9>&-
wait "$reconcile_watcher" || fail reconcile-watcher-rc
grep -F -q 'reconcile complete total=1 started=1 stopped=0' "$WATCH_LOG" || fail reconcile-start-log
grep -F -q 'reconcile complete total=1 started=0 stopped=1' "$WATCH_LOG" || fail reconcile-stop-log

printf 'PASS audio UID watcher semantics\n'
