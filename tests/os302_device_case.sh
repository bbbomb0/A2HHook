#!/system/bin/sh
set -eu

MODDIR=${A2H_MODULE_DIR:-/data/adb/modules/a2h_hook}
EXPECTED_VERSION=${A2H_EXPECTED_VERSION:-v1.5.5-fix2}
CUSTOM_PACKAGE=${A2H_TEST_PACKAGE:-com.kugou.android.lite}
HOTUPDATE_PACKAGE=${A2H_HOTUPDATE_PACKAGE:-com.ss.android.ugc.aweme.lite}
HOTUPDATE_PACKAGE_2=${A2H_HOTUPDATE_PACKAGE_2:-com.example.a2h.hotupdate}
EXECUTE=${A2H_EXECUTE:-0}
RESTART_AUDIO=${A2H_RESTART_AUDIO:-0}
APPLIER="$MODDIR/bin/a2h_apply"
CFG="$MODDIR/config"
WORK="/data/local/tmp/a2h_os302_regression.$$"
PENDING=/data/local/tmp/a2h_apply.pending

fail() {
  printf 'FAIL: %s\n' "$*" >&2
  exit 1
}

prop_value() {
  sed -n "s/^$1=//p" "$MODDIR/module.prop" | head -n 1
}

assert_eq() {
  actual=$1
  expected=$2
  label=$3
  [ "$actual" = "$expected" ] || fail "$label expected=$expected actual=${actual:-missing}"
}

run_check() {
  want=$1
  # This helper asserts fields from the patcher's live diagnostic line, so the
  # check itself must remain verbose. Quiet mode is used only for polling below.
  output=$(A2H_QUIET_PREPARE=1 sh "$APPLIER" check "$want" 2>&1) || {
    printf '%s\n' "$output" >&2
    fail "live check $want"
  }
  printf '%s\n' "$output"
}

wait_applier_idle() {
  idle_label=$1
  idle_wait=0
  while sh "$APPLIER" busy >/dev/null 2>&1 || [ -f "$PENDING" ]; do
    if [ "$idle_wait" -ge 20 ]; then
      printf 'FAIL: applier did not become idle label=%s pending=%s\n' \
        "$idle_label" "$([ -f "$PENDING" ] && printf yes || printf no)" >&2
      return 1
    fi
    sleep 1
    idle_wait=$((idle_wait + 1))
  done
  return 0
}

write_table() {
  slot7_state=$1
  generation=$(date +%s)$$
  cat > "$CFG/.packages.regression.$$" <<EOF
com.kugou.android
com.tencent.qqmusic
com.netease.cloudmusic
cn.kuwo.player
com.miui.player
com.luna.music
$CUSTOM_PACKAGE
com.example.disabled


EOF
  printf '%s\n' 1 1 1 1 1 1 "$slot7_state" 0 0 0 > "$CFG/.states.regression.$$"
  printf '%s\n' "$generation" > "$CFG/.generation.regression.$$"
  chmod 0644 "$CFG/.packages.regression.$$" "$CFG/.states.regression.$$" "$CFG/.generation.regression.$$"
  mv -f "$CFG/.packages.regression.$$" "$CFG/packages.txt"
  mv -f "$CFG/.states.regression.$$" "$CFG/package_states"
  mv -f "$CFG/.generation.regression.$$" "$CFG/config_generation"
}

restart_audio_hal() {
  old_pid=$(pidof android.hardware.audio.service-aidl.mediatek 2>/dev/null || true)
  [ -n "$old_pid" ] || fail "audio HAL PID missing before restart"
  setprop ctl.restart vendor.audio-hal-aidl
  new_pid=
  attempt=0
  while [ "$attempt" -lt 40 ]; do
    attempt=$((attempt + 1))
    sleep 1
    new_pid=$(pidof android.hardware.audio.service-aidl.mediatek 2>/dev/null || true)
    [ -n "$new_pid" ] && [ "$new_pid" != "$old_pid" ] && break
  done
  [ -n "$new_pid" ] && [ "$new_pid" != "$old_pid" ] || fail "audio HAL did not restart old=$old_pid new=${new_pid:-none}"
  printf 'PID_RESTART old=%s new=%s\n' "$old_pid" "$new_pid"
}

restore_config() {
  [ -d "$WORK/backup" ] || return 0
  wait_applier_idle restore-before-write || return 1
  for name in state packages.txt package_states config_generation .package_baseline config_snapshot revision; do
    if [ -f "$WORK/backup/$name" ]; then
      cp -f "$WORK/backup/$name" "$CFG/$name"
    else
      rm -f "$CFG/$name"
    fi
  done
  chmod 0644 "$CFG/state" "$CFG/packages.txt" "$CFG/package_states" "$CFG/config_generation" 2>/dev/null || true
  chmod 0600 "$CFG/.package_baseline" 2>/dev/null || true
  rm -f "$CFG/applied_snapshot" "$CFG/applied_revision" "$CFG/last_pid" 2>/dev/null
  if ! A2H_REASON=regression-restore A2H_APPLY_ATTEMPTS=2 sh "$APPLIER" apply >/dev/null 2>&1; then
    printf 'FAIL: original config restored on disk but native re-apply failed; backup=%s\n' "$WORK/backup" >&2
    return 1
  fi
  restore_mode=$(cat "$CFG/state" 2>/dev/null | tr -d '\r' | head -n 1)
  restore_want=whitelist
  [ "$restore_mode" = enabled ] && restore_want=global
  if ! A2H_QUIET_CHECK=1 A2H_QUIET_PREPARE=1 sh "$APPLIER" check "$restore_want" >/dev/null 2>&1; then
    printf 'FAIL: original config restored but live check failed mode=%s backup=%s\n' "$restore_want" "$WORK/backup" >&2
    return 1
  fi
  for name in state packages.txt package_states config_generation .package_baseline config_snapshot revision; do
    [ ! -f "$WORK/backup/$name" ] || cmp -s "$WORK/backup/$name" "$CFG/$name" || {
      printf 'FAIL: restored config differs file=%s backup=%s\n' "$name" "$WORK/backup" >&2
      return 1
    }
  done
  wait_applier_idle restore-after-check || return 1
  rm -rf "$WORK"
}

cleanup() {
  cleanup_status=$?
  trap - EXIT INT TERM HUP
  restore_config || exit 1
  exit "$cleanup_status"
}

abort_test() {
  trap - EXIT INT TERM HUP
  restore_config || exit 1
  exit 130
}

system=$(getprop ro.build.version.incremental)
device=$(getprop ro.product.device)
version=$(prop_value version)
assert_eq "$device" dali device
assert_eq "$system" OS3.0.302.0.WONCNXM system
assert_eq "$version" "$EXPECTED_VERSION" module-version
[ -x "$APPLIER" ] || fail "applier is not executable"
[ -x "$MODDIR/bin/a2h_patch" ] || fail "patcher is not executable"
patch_version=$($MODDIR/bin/a2h_patch --help 2>&1 | sed -n 's/^a2h_patch //p' | head -n 1)
assert_eq "v$patch_version" "$EXPECTED_VERSION" patcher-version
printf 'PREFLIGHT_OK device=%s system=%s version=%s pid=%s\n' \
  "$device" "$system" "$version" "$(pidof android.hardware.audio.service-aidl.mediatek)"

[ "$EXECUTE" = 1 ] || exit 0

wait_applier_idle pre-test || fail "applier busy before test"
mkdir -p "$WORK/backup"
for name in state packages.txt package_states config_generation .package_baseline config_snapshot revision; do
  [ ! -f "$CFG/$name" ] || cp -f "$CFG/$name" "$WORK/backup/$name"
done
trap cleanup EXIT
trap abort_test INT TERM HUP

audio_restart_result=skipped
round=1
while [ "$round" -le 2 ]; do
  printf 'ROUND_%s_BEGIN\n' "$round"
  write_table 1
  A2H_REASON="regression-r${round}-whitelist7" A2H_APPLY_ATTEMPTS=2 sh "$APPLIER" disable >/dev/null
  output=$(run_check whitelist)
  printf '%s\n' "$output" | grep -q 'active_ptrs=7 config_active=7 mismatch=0' || fail "round $round custom slot enabled mismatch"
  pkg_hash=$(sha256sum "$CFG/packages.txt" | awk '{print $1}')
  state_hash=$(sha256sum "$CFG/package_states" | awk '{print $1}')

  A2H_REASON="regression-r${round}-global" A2H_APPLY_ATTEMPTS=2 sh "$APPLIER" enable >/dev/null
  run_check global >/dev/null
  assert_eq "$(sha256sum "$CFG/packages.txt" | awk '{print $1}')" "$pkg_hash" "round-$round global package preservation"
  assert_eq "$(sha256sum "$CFG/package_states" | awk '{print $1}')" "$state_hash" "round-$round global state preservation"

  A2H_REASON="regression-r${round}-whitelist-return" A2H_APPLY_ATTEMPTS=2 sh "$APPLIER" disable >/dev/null
  output=$(run_check whitelist)
  printf '%s\n' "$output" | grep -q 'active_ptrs=7 config_active=7 mismatch=0' || fail "round $round whitelist return mismatch"

  write_table 0
  A2H_REASON="regression-r${round}-slot7-off" A2H_APPLY_ATTEMPTS=2 sh "$APPLIER" apply >/dev/null
  output=$(run_check whitelist)
  printf '%s\n' "$output" | grep -q 'active_ptrs=6 config_active=6 mismatch=0' || fail "round $round custom slot off mismatch"

  write_table 1
  A2H_REASON="regression-r${round}-slot7-on" A2H_APPLY_ATTEMPTS=2 sh "$APPLIER" apply >/dev/null
  output=$(run_check whitelist)
  printf '%s\n' "$output" | grep -q 'active_ptrs=7 config_active=7 mismatch=0' || fail "round $round custom slot re-enable mismatch"

  if [ "$RESTART_AUDIO" = 1 ]; then
    restart_audio_hal
    attempt=0
    restart_check_ok=0
    while [ "$attempt" -lt 40 ]; do
      attempt=$((attempt + 1))
      sleep 1
      if A2H_QUIET_CHECK=1 A2H_QUIET_PREPARE=1 sh "$APPLIER" check whitelist >/dev/null 2>&1; then
        restart_check_ok=1
        break
      fi
    done
    [ "$restart_check_ok" = 1 ] || fail "round $round watcher did not reapply after PID restart"
    output=$(run_check whitelist)
    printf '%s\n' "$output" | grep -q 'active_ptrs=7 config_active=7 mismatch=0' || fail "round $round post-restart table mismatch"
    audio_restart_result=verified
  fi

  assert_eq "$(cat "$CFG/state")" disabled "round-$round persisted mode"
  printf 'ROUND_%s_PASS\n' "$round"
  round=$((round + 1))
done

# File-manager path: change only packages.txt and let service.sh debounce,
# normalize, queue, apply, and verify slot 8 without a manual applier call.
write_table 1
A2H_REASON=regression-hotupdate-baseline A2H_APPLY_ATTEMPTS=2 sh "$APPLIER" disable >/dev/null
revision_before=$(cat "$CFG/revision")
hot_tmp="$CFG/.packages.hotupdate.$$"
awk -v package="$HOTUPDATE_PACKAGE" 'NR == 8 { print package; next } { print }' "$CFG/packages.txt" > "$hot_tmp"
chmod 0644 "$hot_tmp"
mv -f "$hot_tmp" "$CFG/packages.txt"
hot_wait=0
hot_ok=0
while [ "$hot_wait" -lt 10 ]; do
  sleep 1
  hot_wait=$((hot_wait + 1))
  hot_state=$(sed -n '8p' "$CFG/package_states")
  hot_revision=$(cat "$CFG/revision" 2>/dev/null || true)
  hot_applied=$(cat "$CFG/applied_revision" 2>/dev/null || true)
  hot_baseline=$(sed -n '8p' "$CFG/.package_baseline" 2>/dev/null || true)
  [ "$hot_state" = 1 ] && [ "$hot_revision" = "$hot_applied" ] &&
    [ "$hot_baseline" = "$HOTUPDATE_PACKAGE" ] && { hot_ok=1; break; }
done
[ "$hot_ok" = 1 ] || fail "slot 8 file-manager hot update timed out"
revision_after=$(cat "$CFG/revision")
assert_eq "$revision_after" "$((revision_before + 1))" "slot-8 hot-update revision"
output=$(run_check whitelist)
printf '%s\n' "$output" | grep -q 'active_ptrs=8 config_active=8 mismatch=0' || fail "slot 8 hot-update native table mismatch"
printf '%s\n' "$output" | grep -q "active_ptr\[7\].*$HOTUPDATE_PACKAGE" || fail "slot 8 hot-update package pointer mismatch"
printf 'HOTUPDATE_SLOT8_PASS wait=%ss revision=%s->%s package=%s\n' "$hot_wait" "$revision_before" "$revision_after" "$HOTUPDATE_PACKAGE"

# Simulate a file manager's multi-stage save. The intermediate table exists for
# less than one debounce interval and must never become its own revision.
write_table 1
A2H_REASON=regression-hotupdate-multistage-baseline A2H_APPLY_ATTEMPTS=2 sh "$APPLIER" disable >/dev/null
revision_before=$(cat "$CFG/revision")
intermediate_tmp="$CFG/.packages.intermediate.$$"
awk -v package="$HOTUPDATE_PACKAGE_2" '
  NR == 8 { print ""; next }
  NR == 9 { print package; next }
  { print }
' "$CFG/packages.txt" > "$intermediate_tmp"
chmod 0644 "$intermediate_tmp"
mv -f "$intermediate_tmp" "$CFG/packages.txt"
sleep 1
final_tmp="$CFG/.packages.final.$$"
awk -v package="$HOTUPDATE_PACKAGE_2" '
  NR == 8 { print package; next }
  NR == 9 { print ""; next }
  { print }
' "$CFG/packages.txt" > "$final_tmp"
chmod 0644 "$final_tmp"
mv -f "$final_tmp" "$CFG/packages.txt"
multi_wait=0
multi_ok=0
while [ "$multi_wait" -lt 10 ]; do
  sleep 1
  multi_wait=$((multi_wait + 1))
  multi_state=$(sed -n '8p' "$CFG/package_states")
  multi_revision=$(cat "$CFG/revision" 2>/dev/null || true)
  multi_applied=$(cat "$CFG/applied_revision" 2>/dev/null || true)
  multi_baseline8=$(sed -n '8p' "$CFG/.package_baseline" 2>/dev/null || true)
  multi_baseline9=$(sed -n '9p' "$CFG/.package_baseline" 2>/dev/null || true)
  [ "$multi_state" = 1 ] && [ "$multi_revision" = "$multi_applied" ] &&
    [ "$multi_baseline8" = "$HOTUPDATE_PACKAGE_2" ] && [ -z "$multi_baseline9" ] && { multi_ok=1; break; }
done
[ "$multi_ok" = 1 ] || fail "slot 8 multi-stage hot update timed out"
revision_after=$(cat "$CFG/revision")
assert_eq "$revision_after" "$((revision_before + 1))" "slot-8 multi-stage revision"
output=$(run_check whitelist)
printf '%s\n' "$output" | grep -q 'active_ptrs=8 config_active=8 mismatch=0' || fail "slot 8 multi-stage native table mismatch"
printf '%s\n' "$output" | grep -q "active_ptr\[7\].*$HOTUPDATE_PACKAGE_2" || fail "slot 8 multi-stage package pointer mismatch"
printf 'HOTUPDATE_MULTISTAGE_PASS wait=%ss revision=%s->%s package=%s\n' "$multi_wait" "$revision_before" "$revision_after" "$HOTUPDATE_PACKAGE_2"

printf 'PASS OS3.0.302 two-round native/config plus slot-8 hot-update regression audio-restart=%s\n' "$audio_restart_result"
