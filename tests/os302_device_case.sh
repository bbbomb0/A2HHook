#!/system/bin/sh
set -eu

MODDIR=${A2H_MODULE_DIR:-/data/adb/modules/a2h_hook}
EXPECTED_VERSION=${A2H_EXPECTED_VERSION:-v1.5.5-fix}
CUSTOM_PACKAGE=${A2H_TEST_PACKAGE:-com.kugou.android.lite}
EXECUTE=${A2H_EXECUTE:-0}
RESTART_AUDIO=${A2H_RESTART_AUDIO:-0}
APPLIER="$MODDIR/bin/a2h_apply"
CFG="$MODDIR/config"
WORK="/data/local/tmp/a2h_os302_regression.$$"

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
  for name in state packages.txt package_states config_generation; do
    if [ -f "$WORK/backup/$name" ]; then
      cp -f "$WORK/backup/$name" "$CFG/$name"
    else
      rm -f "$CFG/$name"
    fi
  done
  chmod 0644 "$CFG/state" "$CFG/packages.txt" "$CFG/package_states" "$CFG/config_generation" 2>/dev/null || true
  A2H_REASON=regression-restore A2H_APPLY_ATTEMPTS=2 sh "$APPLIER" apply >/dev/null 2>&1 || true
  rm -rf "$WORK"
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

mkdir -p "$WORK/backup"
for name in state packages.txt package_states config_generation; do
  [ ! -f "$CFG/$name" ] || cp -f "$CFG/$name" "$WORK/backup/$name"
done
trap restore_config EXIT INT TERM HUP

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
    while [ "$attempt" -lt 40 ]; do
      attempt=$((attempt + 1))
      sleep 1
      if A2H_QUIET_CHECK=1 A2H_QUIET_PREPARE=1 sh "$APPLIER" check whitelist >/dev/null 2>&1; then break; fi
    done
    [ "$attempt" -lt 40 ] || fail "round $round watcher did not reapply after PID restart"
    output=$(run_check whitelist)
    printf '%s\n' "$output" | grep -q 'active_ptrs=7 config_active=7 mismatch=0' || fail "round $round post-restart table mismatch"
  fi

  marker_payload=$(printf '__A2H_DEVICE_STATE__\n'; cat "$CFG/state"; printf '__A2H_DEVICE_PACKAGES__\n'; cat "$CFG/packages.txt"; printf '__A2H_DEVICE_STATES__\n'; cat "$CFG/package_states"; printf '__A2H_DEVICE_END__\n')
  printf '%s\n' "$marker_payload" | grep -q '__A2H_DEVICE_END__' || fail "round $round WebUI marker payload"
  assert_eq "$(printf '%s\n' "$marker_payload" | sed -n '/__A2H_DEVICE_STATE__/{n;p;q;}')" disabled "round-$round WebUI state"
  printf 'ROUND_%s_PASS\n' "$round"
  round=$((round + 1))
done

printf 'PASS OS3.0.302 two-round native/config/PID/WebUI-marker regression\n'
