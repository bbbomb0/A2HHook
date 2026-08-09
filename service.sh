#!/system/bin/sh

# KernelSU/APatch/Magisk late-start service. Configuration and patching are
# delegated to bin/a2h_apply so the module does not need a root action.sh.

MODDIR=${0%/*}
CONFIG_EVENT_MARKER=/data/local/tmp/a2h_config.changed
if [ "${A2H_INOTIFY_CALLBACK:-0}" = "1" ]; then
  # inotifyd invokes: PROG EVENTS WATCHED_PATH [DIRECTORY_ENTRY].
  config_event_name=${3:-${2##*/}}
  case "$config_event_name" in
    state|game_auto_pause|packages.txt|package_states|config_generation)
      : > "$CONFIG_EVENT_MARKER" 2>/dev/null
      ;;
  esac
  exit $?
fi

APPLIER="$MODDIR/bin/a2h_apply"
PATCHER="$MODDIR/bin/a2h_patch"
CFG_DIR="$MODDIR/config"
CFG_STATE="$CFG_DIR/state"
CFG_GAME_POLICY="$CFG_DIR/game_auto_pause"
CFG_PKGS="$CFG_DIR/packages.txt"
CFG_STATES="$CFG_DIR/package_states"
CFG_GENERATION="$CFG_DIR/config_generation"
CFG_SNAPSHOT="$CFG_DIR/config_snapshot"
CFG_REVISION="$CFG_DIR/revision"
APPLIED_SNAPSHOT="$CFG_DIR/applied_snapshot"
APPLIED_REVISION="$CFG_DIR/applied_revision"
LAST_PID_FILE="$CFG_DIR/last_pid"
NOTIFICATION_STATE_FILE="$CFG_DIR/notification_state"
NOTIFICATION_RETRY_FILE="$CFG_DIR/notification_retry_state"
NOTIFICATION_LOCK_DIR="$CFG_DIR/.notification_lock"
TMP_PKGS=/data/local/tmp/a2h_packages.txt
LOG="$MODDIR/a2h_patch.log"
COMPANION_APK="$MODDIR/companion/a2h_companion.apk"
COMPANION_PACKAGE=io.github.bbbomb0.a2hhook
COMPANION_VERSION_CODE=1561

# A successfully installed module supersedes any one-shot companion cleanup
# left by an earlier uninstall. The cleanup script independently checks both
# module locations before touching the package.
rm -f \
  /data/adb/service.d/a2h_hook_companion_cleanup.sh \
  /data/adb/service.d/.a2h_hook_companion_cleanup.* 2>/dev/null

ts() { date '+%F %T'; }
log() { printf '[%s] %s\n' "$(ts)" "$*" >> "$LOG" 2>/dev/null; }

raw_config_signature() {
  {
    for raw_config_file in "$CFG_STATE" "$CFG_GAME_POLICY" "$CFG_PKGS" "$CFG_STATES" "$CFG_GENERATION"; do
      if [ -f "$raw_config_file" ]; then
        printf '%s\n' "${raw_config_file##*/}"
        cksum < "$raw_config_file" 2>/dev/null
      else
        printf '%s\nmissing\n' "${raw_config_file##*/}"
      fi
    done
  } | cksum 2>/dev/null | awk '{print $1 ":" $2}'
}

start_config_inotify() {
  config_inotify_enabled=0
  config_inotify_pid=
  command -v inotifyd >/dev/null 2>&1 || return 1
  A2H_INOTIFY_CALLBACK=1 inotifyd "$MODDIR/service.sh" \
    "$CFG_DIR:mnyd" "$CFG_STATE:w" "$CFG_GAME_POLICY:w" "$CFG_PKGS:w" \
    "$CFG_STATES:w" "$CFG_GENERATION:w" >/dev/null 2>&1 &
  config_inotify_pid=$!
  if kill -0 "$config_inotify_pid" 2>/dev/null; then
    config_inotify_enabled=1
    return 0
  fi
  config_inotify_pid=
  return 1
}

stop_config_inotify() {
  if [ -n "$config_inotify_pid" ]; then
    kill "$config_inotify_pid" 2>/dev/null || true
    wait "$config_inotify_pid" 2>/dev/null || true
  fi
  config_inotify_pid=
  config_inotify_enabled=0
}

refresh_config_inotify() {
  stop_config_inotify
  rm -f "$CONFIG_EVENT_MARKER" 2>/dev/null
  start_config_inotify || true
}

service_cleanup() {
  stop_config_inotify
  rm -f "$CONFIG_EVENT_MARKER" 2>/dev/null
}

find_hal_pid() {
  service_pid=$(pidof android.hardware.audio.service-aidl.mediatek 2>/dev/null | awk '{print $1}')
  [ -n "$service_pid" ] && { printf '%s\n' "$service_pid"; return 0; }
  service_pid=$(pgrep -f android.hardware.audio.service-aidl.mediatek 2>/dev/null | head -n 1)
  [ -n "$service_pid" ] && { printf '%s\n' "$service_pid"; return 0; }
  service_pid=$(pidof android.hardware.audio.service 2>/dev/null | awk '{print $1}')
  [ -n "$service_pid" ] && { printf '%s\n' "$service_pid"; return 0; }
  for service_cmdline in /proc/[0-9]*/cmdline; do
    [ -r "$service_cmdline" ] || continue
    service_pid=${service_cmdline#/proc/}
    service_pid=${service_pid%%/*}
    if grep -a -F -q android.hardware.audio.service-aidl.mediatek "$service_cmdline" 2>/dev/null; then
      printf '%s\n' "$service_pid"
      return 0
    fi
    if grep -a -F -q android.hardware.audio.service "$service_cmdline" 2>/dev/null ||
       grep -a -F -q audio.service-aidl "$service_cmdline" 2>/dev/null; then
      if grep -a -F -q 'audio.primary.' "/proc/$service_pid/maps" 2>/dev/null; then
        printf '%s\n' "$service_pid"
        return 0
      fi
    fi
  done
  return 1
}

apply_once() {
  service_reason=$1
  A2H_REASON="$service_reason" A2H_APPLY_ATTEMPTS=1 sh "$APPLIER" apply >> "$LOG" 2>&1
}

ensure_companion_installed() {
  [ -f "$COMPANION_APK" ] || {
    log "companion install skipped reason=apk-missing"
    return 1
  }
  companion_wait=0
  while [ "$(getprop sys.boot_completed 2>/dev/null)" != "1" ]; do
    [ "$companion_wait" -lt 60 ] || {
      log "companion install deferred reason=boot-timeout"
      return 1
    }
    sleep 2
    companion_wait=$((companion_wait + 1))
  done
  command -v pm >/dev/null 2>&1 || {
    log "companion install skipped reason=pm-missing"
    return 1
  }
  companion_installed_code=$(dumpsys package "$COMPANION_PACKAGE" 2>/dev/null |
    sed -n 's/^[[:space:]]*versionCode=\([0-9][0-9]*\).*/\1/p' | head -n 1)
  companion_expected_hash=
  companion_installed_hash=
  companion_installed_apk=$(pm path "$COMPANION_PACKAGE" 2>/dev/null |
    sed -n 's/^package://p' | head -n 1)
  if command -v sha256sum >/dev/null 2>&1; then
    companion_expected_hash=$(sha256sum "$COMPANION_APK" 2>/dev/null | awk '{print $1}')
    [ -z "$companion_installed_apk" ] ||
      companion_installed_hash=$(sha256sum "$companion_installed_apk" 2>/dev/null | awk '{print $1}')
  fi
  if [ "$companion_installed_code" = "$COMPANION_VERSION_CODE" ] &&
     [ -n "$companion_expected_hash" ] &&
     [ "$companion_installed_hash" = "$companion_expected_hash" ]; then
    log "companion ready package=$COMPANION_PACKAGE versionCode=$COMPANION_VERSION_CODE sha256=$companion_expected_hash"
    return 0
  fi
  companion_uninstall_result=$(pm uninstall "$COMPANION_PACKAGE" 2>&1)
  if pm path "$COMPANION_PACKAGE" >/dev/null 2>&1; then
    companion_uninstall_result=$(pm uninstall --user 0 "$COMPANION_PACKAGE" 2>&1)
  fi
  if pm path "$COMPANION_PACKAGE" >/dev/null 2>&1; then
    log "companion uninstall FAIL previous=${companion_installed_code:-none} output=$companion_uninstall_result"
    return 1
  fi
  companion_result=$(pm install --user 0 "$COMPANION_APK" 2>&1)
  companion_rc=$?
  if [ "$companion_rc" -eq 0 ]; then
    log "companion clean-installed package=$COMPANION_PACKAGE versionCode=$COMPANION_VERSION_CODE previous=${companion_installed_code:-none}"
    return 0
  fi
  log "companion install FAIL rc=$companion_rc previous=${companion_installed_code:-none} output=$companion_result"
  return "$companion_rc"
}

applier_busy() {
  sh "$APPLIER" busy >/dev/null 2>&1
}

notification_text() {
  notification_result=$1
  notification_mode=$(cat "$CFG_STATE" 2>/dev/null | tr -d '\r' | head -n 1)
  notification_active=$(awk 'NF { count++ } END { print count + 0 }' "$TMP_PKGS" 2>/dev/null)
  [ -n "$notification_active" ] || notification_active=0
  if [ "$notification_result" = "success" ]; then
    if [ "$notification_mode" = "enabled" ]; then
      printf '%s\n' '开机自动加载成功：全局音乐触感已通过运行状态校验。'
    else
      printf '开机自动加载成功：白名单音乐触感已通过运行状态校验，当前启用 %s 个应用。\n' "$notification_active"
    fi
  else
    printf '%s\n' '开机自动加载失败：未通过运行状态校验，请检查模块目录中的 a2h_patch.log 与 action.log。'
  fi
}

notification_runtime_result() {
  notification_pid=$(find_hal_pid)
  notification_last_pid=$(cat "$LAST_PID_FILE" 2>/dev/null)
  notification_snapshot_state=$(A2H_QUIET_PREPARE=1 sh "$APPLIER" snapshot-state 2>/dev/null)
  case "$notification_snapshot_state" in
    *'|'*) notification_snapshot=${notification_snapshot_state%%|*} ;;
    *) notification_snapshot= ;;
  esac
  notification_revision=$(cat "$CFG_REVISION" 2>/dev/null)
  notification_applied_snapshot=$(cat "$APPLIED_SNAPSHOT" 2>/dev/null)
  notification_applied_revision=$(cat "$APPLIED_REVISION" 2>/dev/null)
  if [ -n "$notification_pid" ] &&
     [ "$notification_pid" = "$notification_last_pid" ] &&
     [ -n "$notification_snapshot" ] &&
     [ "$notification_snapshot" = "$notification_applied_snapshot" ] &&
     [ -n "$notification_revision" ] &&
     [ "$notification_revision" = "$notification_applied_revision" ]; then
    printf '%s\n' success
  else
    printf '%s\n' failure
  fi
}

record_notification_marker() {
  notification_marker_file=$1
  notification_marker_value=$2
  notification_marker_tmp="${notification_marker_file}.$$"
  printf '%s\n' "$notification_marker_value" > "$notification_marker_tmp" 2>/dev/null || return 1
  chmod 600 "$notification_marker_tmp" 2>/dev/null || true
  mv -f "$notification_marker_tmp" "$notification_marker_file" 2>/dev/null
}

record_notification_state() {
  record_notification_marker "$NOTIFICATION_STATE_FILE" "$1"
}

post_notification_once() {
  : > "$notification_tmp" 2>/dev/null || return 1
  if /system/bin/cmd notification post -S bigtext -t "$notification_title" "$notification_tag" "$notification_body" > "$notification_tmp" 2>&1; then
    notification_style=bigtext
    return 0
  fi
  log "boot notification bigtext failed; trying plain title"
  cat "$notification_tmp" >> "$LOG" 2>/dev/null

  if /system/bin/cmd notification post -t "$notification_title" "$notification_tag" "$notification_body" > "$notification_tmp" 2>&1; then
    notification_style=plain
    return 0
  fi
  log "boot notification titled form failed; trying minimal form"
  cat "$notification_tmp" >> "$LOG" 2>/dev/null

  if /system/bin/cmd notification post "$notification_tag" "$notification_body" > "$notification_tmp" 2>&1; then
    notification_style=minimal
    return 0
  fi
  cat "$notification_tmp" >> "$LOG" 2>/dev/null
  return 1
}

post_boot_notification() {
  notification_requested=$1
  notification_wait=0
  while [ "$notification_wait" -lt 60 ]; do
    [ "$(getprop sys.boot_completed 2>/dev/null)" = "1" ] && break
    sleep 2
    notification_wait=$((notification_wait + 1))
  done

  if ! mkdir "$NOTIFICATION_LOCK_DIR" 2>/dev/null; then
    log "notification worker already active"
    return 0
  fi

  notification_result=$(notification_runtime_result)
  [ -n "$notification_result" ] || notification_result=failure
  if [ "$notification_result" != "$notification_requested" ]; then
    log "notification state refreshed requested=$notification_requested actual=$notification_result"
  fi
  notification_previous=$(cat "$NOTIFICATION_STATE_FILE" 2>/dev/null | tr -d '\r' | head -n 1)
  if [ "$notification_previous" = "$notification_result" ]; then
    log "notification unchanged result=$notification_result"
    rmdir "$NOTIFICATION_LOCK_DIR" 2>/dev/null
    return 0
  fi

  notification_exhausted=$(cat "$NOTIFICATION_RETRY_FILE" 2>/dev/null | tr -d '\r' | head -n 1)
  if [ "$notification_exhausted" = "$notification_result" ]; then
    log "notification retry limit already reached result=$notification_result"
    rmdir "$NOTIFICATION_LOCK_DIR" 2>/dev/null
    return 1
  fi
  [ -z "$notification_exhausted" ] || rm -f "$NOTIFICATION_RETRY_FILE" 2>/dev/null

  notification_body=$(notification_text "$notification_result")
  notification_title='A2H 音乐触感'
  notification_tag=a2h_hook
  notification_tmp="/data/local/tmp/.a2h_notification.$$"
  notification_attempt=1
  notification_delay=2
  while [ "$notification_attempt" -le 3 ]; do
    if post_notification_once; then
      if record_notification_state "$notification_result"; then
        rm -f "$NOTIFICATION_RETRY_FILE" 2>/dev/null
        log "boot notification posted result=$notification_result style=$notification_style attempt=$notification_attempt"
      else
        log "boot notification posted but state record failed result=$notification_result"
      fi
      rm -f "$notification_tmp" 2>/dev/null
      rmdir "$NOTIFICATION_LOCK_DIR" 2>/dev/null
      return 0
    fi
    log "boot notification post failed result=$notification_result attempt=$notification_attempt/3"
    [ "$notification_attempt" -ge 3 ] && break
    sleep "$notification_delay"
    notification_delay=$((notification_delay * 2))
    notification_attempt=$((notification_attempt + 1))
  done

  record_notification_marker "$NOTIFICATION_RETRY_FILE" "$notification_result" || true
  log "boot notification FAIL result=$notification_result attempts=3; retry limit reached"
  rm -f "$notification_tmp" 2>/dev/null
  rmdir "$NOTIFICATION_LOCK_DIR" 2>/dev/null
  return 1
}

set_runtime_status() {
  next_runtime_status=$1
  if [ "$runtime_status" = "$next_runtime_status" ]; then
    notification_recorded=$(cat "$NOTIFICATION_STATE_FILE" 2>/dev/null | tr -d '\r' | head -n 1)
    [ "$notification_recorded" = "$next_runtime_status" ] && return 0
    notification_exhausted=$(cat "$NOTIFICATION_RETRY_FILE" 2>/dev/null | tr -d '\r' | head -n 1)
    [ "$notification_exhausted" = "$next_runtime_status" ] && return 0
    [ "$(getprop sys.boot_completed 2>/dev/null)" = "1" ] || return 0
    post_boot_notification "$next_runtime_status" &
    return 0
  fi
  previous_runtime_status=${runtime_status:-unknown}
  runtime_status=$next_runtime_status
  log "runtime status transition $previous_runtime_status -> $runtime_status"
  rm -f "$NOTIFICATION_RETRY_FILE" 2>/dev/null
  if [ "$(getprop sys.boot_completed 2>/dev/null)" = "1" ]; then
    post_boot_notification "$runtime_status" &
  fi
}

module_version=$(sed -n 's/^version=//p' "$MODDIR/module.prop" 2>/dev/null | head -n 1)
[ -n "$module_version" ] || module_version=unknown
printf '[a2h_hook] %s %s\n' "$module_version" "$(date)" > "$LOG" 2>/dev/null

mkdir -p "$CFG_DIR" /data/local/tmp 2>/dev/null
rm -f "$NOTIFICATION_STATE_FILE" "$NOTIFICATION_RETRY_FILE" 2>/dev/null
rmdir "$NOTIFICATION_LOCK_DIR" 2>/dev/null
chmod 755 "$APPLIER" "$PATCHER" 2>/dev/null

if [ ! -f "$APPLIER" ] || [ ! -f "$PATCHER" ]; then
  log "boot apply FAIL missing executable applier=$([ -f "$APPLIER" ] && printf yes || printf no) patcher=$([ -f "$PATCHER" ] && printf yes || printf no)"
  post_boot_notification failure &
  exit 1
fi

log "boot auto-apply start"
ensure_companion_installed &
boot_ok=0
boot_try=1
boot_delay=1
boot_attempts=0
boot_failure_reason=retry-exhausted
while [ "$boot_try" -le 8 ]; do
  boot_attempts=$boot_try
  boot_rc=0
  if apply_once "boot#$boot_try"; then
    boot_ok=1
    break
  else
    boot_rc=$?
  fi
  if [ "$boot_rc" -eq 74 ]; then
    boot_failure_reason=metadata-io
    log "boot auto-apply metadata commit failed rc=$boot_rc; deferring to watcher bounded backoff"
    break
  fi
  log "boot auto-apply retry=$boot_try delay=${boot_delay}s"
  sleep "$boot_delay"
  if [ "$boot_delay" -lt 8 ]; then
    boot_delay=$((boot_delay * 2))
    [ "$boot_delay" -le 8 ] || boot_delay=8
  fi
  boot_try=$((boot_try + 1))
done

if [ "$boot_ok" = "1" ]; then
  boot_mode=$(cat "$CFG_STATE" 2>/dev/null | tr -d '\r' | head -n 1)
  boot_snapshot=$(cat "$APPLIED_SNAPSHOT" 2>/dev/null)
  log "boot auto-apply verified mode=$boot_mode snapshot=${boot_snapshot:-none} attempts=$boot_try"
  runtime_status=success
  post_boot_notification success &
else
  log "boot auto-apply incomplete reason=$boot_failure_reason attempts=$boot_attempts; watcher will continue with bounded backoff"
  runtime_status=failure
  post_boot_notification failure &
fi

config_inotify_pid=
config_inotify_enabled=0
rm -f "$CONFIG_EVENT_MARKER" 2>/dev/null
if start_config_inotify; then
  log "watcher config events=inotifyd pid=$config_inotify_pid"
else
  log "watcher config events=polling fallback=${watch_tick_seconds:-2}s"
fi
trap 'exit 0' INT TERM HUP
trap service_cleanup EXIT

last_pid=$(cat "$LAST_PID_FILE" 2>/dev/null)
last_raw_signature=$(raw_config_signature)
watch_failures=0
watch_cycle=0
failed_key=
failure_delay_ticks=25
failure_retry_cycle=0
pending_raw_signature=
pending_stable_ticks=0
pending_busy_logged=0
watch_tick_seconds=2
# External file managers can expose a multi-stage save for longer than one
# two-second sample under load. Require two full unchanged intervals after the
# first observation; WebUI and tile writes still use the immediate queue path.
watch_stable_ticks=3
watch_health_ticks=15
config_poll_grace_ticks=0
next_health_cycle=$watch_health_ticks
log "watcher start pid=${last_pid:-none} tick=${watch_tick_seconds}s stable_ticks=$watch_stable_ticks health_interval=$((watch_tick_seconds * watch_health_ticks))s"

while true; do
  sleep "$watch_tick_seconds"
  watch_cycle=$((watch_cycle + 1))

  health_due=0
  [ "$watch_cycle" -ge "$next_health_cycle" ] && health_due=1
  if [ "$config_inotify_enabled" = "1" ] &&
     ! kill -0 "$config_inotify_pid" 2>/dev/null; then
    config_inotify_enabled=0
    config_inotify_pid=
    log "watcher inotifyd exited; using signature polling fallback"
  fi

  signature_needed=0
  [ "$config_inotify_enabled" = "0" ] && signature_needed=1
  [ -n "$pending_raw_signature" ] && signature_needed=1
  [ "$health_due" = "1" ] && signature_needed=1
  [ "$config_poll_grace_ticks" -gt 0 ] && signature_needed=1
  if [ -f "$CONFIG_EVENT_MARKER" ]; then
    signature_needed=1
    refresh_config_inotify
    # inotifyd can be alive just before its replacement watches are registered.
    # Poll through that short rearm window so an atomic file-manager save cannot
    # disappear until the next 30-second health probe.
    config_poll_grace_ticks=$((watch_stable_ticks + 1))
  fi
  if [ "$signature_needed" = "1" ]; then
    raw_signature_now=$(raw_config_signature)
  else
    raw_signature_now=$last_raw_signature
  fi
  if [ "$config_poll_grace_ticks" -gt 0 ]; then
    config_poll_grace_ticks=$((config_poll_grace_ticks - 1))
  fi

  config_ready=0
  if [ "$raw_signature_now" = "$last_raw_signature" ]; then
    pending_raw_signature=
    pending_stable_ticks=0
    pending_busy_logged=0
  else
    if [ "$raw_signature_now" != "$pending_raw_signature" ]; then
      pending_raw_signature=$raw_signature_now
      pending_stable_ticks=1
      pending_busy_logged=0
      log "watcher config edit observed signature=${raw_signature_now:-none}"
    else
      pending_stable_ticks=$((pending_stable_ticks + 1))
    fi
    if [ "$pending_stable_ticks" -ge "$watch_stable_ticks" ]; then
      if applier_busy; then
        if [ "$pending_busy_logged" = "0" ]; then
          log "watcher stable config deferred while applier is busy signature=$pending_raw_signature"
          pending_busy_logged=1
        fi
      else
        config_ready=1
        log "watcher stable config edit detected signature=$pending_raw_signature stable_ticks=$pending_stable_ticks"
      fi
    fi
  fi

  # Never let the slower health path normalize a file manager's partial save.
  if [ -n "$pending_raw_signature" ] && [ "$config_ready" = "0" ]; then
    continue
  fi

  if [ "$config_ready" = "0" ] && [ "$health_due" = "0" ]; then
    continue
  fi
  if [ "$config_ready" = "0" ] && applier_busy; then
    continue
  fi

  snapshot_state=$(A2H_QUIET_PREPARE=1 sh "$APPLIER" snapshot-state 2>/dev/null)
  snapshot_rc=$?
  case "$snapshot_state" in
    *'|'*)
      current_snapshot=${snapshot_state%%|*}
      prepared_raw_signature=${snapshot_state#*|}
      ;;
    *)
      current_snapshot=
      prepared_raw_signature=
      ;;
  esac
  if [ "$snapshot_rc" -ne 0 ] || [ -z "$current_snapshot" ] || [ -z "$prepared_raw_signature" ]; then
    watch_failures=$((watch_failures + 1))
    [ $((watch_failures % 6)) -eq 0 ] && log "watcher config prepare failed x$watch_failures"
    set_runtime_status failure
    continue
  fi

  last_raw_signature=$prepared_raw_signature
  raw_signature_after=$(raw_config_signature)
  if [ "$raw_signature_after" != "$prepared_raw_signature" ]; then
    pending_raw_signature=$raw_signature_after
    pending_stable_ticks=1
    pending_busy_logged=0
    log "watcher config changed across snapshot; retrying after quiet window processed=$prepared_raw_signature latest=$raw_signature_after"
    continue
  fi
  pending_raw_signature=
  pending_stable_ticks=0
  pending_busy_logged=0
  next_health_cycle=$((watch_cycle + watch_health_ticks))
  current_pid=$(find_hal_pid)
  current_revision=$(cat "$CFG_REVISION" 2>/dev/null)
  applied_snapshot=$(cat "$APPLIED_SNAPSHOT" 2>/dev/null)
  applied_revision=$(cat "$APPLIED_REVISION" 2>/dev/null)
  need_apply=0
  apply_reason=

  if [ -z "$current_pid" ]; then
    watch_failures=$((watch_failures + 1))
    [ $((watch_failures % 6)) -eq 0 ] && log "watcher HAL not found x$watch_failures"
    set_runtime_status failure
    continue
  fi

  attempt_key="$current_pid|$current_revision|$current_snapshot"
  if [ "$attempt_key" = "$failed_key" ] && [ "$watch_cycle" -lt "$failure_retry_cycle" ]; then
    next_failure_probe=$((watch_cycle + watch_health_ticks))
    if [ "$failure_retry_cycle" -lt "$next_failure_probe" ]; then
      next_health_cycle=$failure_retry_cycle
    else
      next_health_cycle=$next_failure_probe
    fi
    set_runtime_status failure
    continue
  fi

  if [ "$current_pid" != "$last_pid" ]; then
    need_apply=1
    apply_reason="pid-change:${last_pid:-none}-$current_pid"
    log "watcher HAL pid changed ${last_pid:-none} -> $current_pid"
  fi

  if [ "$current_snapshot" != "$applied_snapshot" ] ||
     [ "$current_revision" != "$applied_revision" ]; then
    need_apply=1
    if [ -n "$apply_reason" ]; then
      apply_reason="$apply_reason,config-change"
    else
      apply_reason=config-change
    fi
    log "watcher config changed applied=${applied_revision:-none}/${applied_snapshot:-none} current=${current_revision:-none}/$current_snapshot"
  fi

  # Stable health probes stop at PID and snapshot metadata. Native --check
  # freezes every HAL thread, so it is reserved for real apply verification.

  if [ "$need_apply" = "1" ]; then
    if apply_once "watch:$apply_reason"; then
      last_pid=$(cat "$LAST_PID_FILE" 2>/dev/null)
      watch_failures=0
      failed_key=
      failure_delay_ticks=25
      failure_retry_cycle=0
      log "watcher apply verified reason=$apply_reason pid=${last_pid:-unknown}"
      set_runtime_status success
    else
      watch_failures=$((watch_failures + 1))
      if [ "$failed_key" = "$attempt_key" ]; then
        failure_delay_ticks=$((failure_delay_ticks * 2))
      else
        failure_delay_ticks=25
      fi
      [ "$failure_delay_ticks" -le 300 ] || failure_delay_ticks=300
      failed_key=$attempt_key
      failure_retry_cycle=$((watch_cycle + failure_delay_ticks))
      failure_delay_seconds=$((failure_delay_ticks * watch_tick_seconds))
      log "watcher apply FAIL reason=$apply_reason failures=$watch_failures retry_in=${failure_delay_seconds}s key=$failed_key"
      set_runtime_status failure
    fi
  else
    last_pid=$current_pid
    watch_failures=0
    failed_key=
    failure_delay_ticks=25
    failure_retry_cycle=0
    set_runtime_status success
  fi
done
