#!/system/bin/sh

# KernelSU/APatch/Magisk late-start service. Configuration and patching are
# delegated to bin/a2h_apply so the module does not need a root action.sh.

MODDIR=${0%/*}
APPLIER="$MODDIR/bin/a2h_apply"
PATCHER="$MODDIR/bin/a2h_patch"
CFG_DIR="$MODDIR/config"
CFG_STATE="$CFG_DIR/state"
CFG_SNAPSHOT="$CFG_DIR/config_snapshot"
APPLIED_SNAPSHOT="$CFG_DIR/applied_snapshot"
LAST_PID_FILE="$CFG_DIR/last_pid"
NOTIFICATION_STATE_FILE="$CFG_DIR/notification_state"
TMP_PKGS=/data/local/tmp/a2h_packages.txt
LOG="$MODDIR/a2h_patch.log"

ts() { date '+%F %T'; }
log() { printf '[%s] %s\n' "$(ts)" "$*" >> "$LOG" 2>/dev/null; }

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
    printf '%s\n' '开机自动加载失败：未通过运行状态校验，请打开模块 WebUI 查看状态并分享日志。'
  fi
}

notification_live_result() {
  notification_mode=$(cat "$CFG_STATE" 2>/dev/null | tr -d '\r' | head -n 1)
  notification_want=whitelist
  [ "$notification_mode" = "enabled" ] && notification_want=global
  if [ -f "$APPLIER" ] &&
     A2H_QUIET_CHECK=1 A2H_QUIET_PREPARE=1 sh "$APPLIER" check "$notification_want" >/dev/null 2>&1; then
    printf '%s\n' success
  else
    printf '%s\n' failure
  fi
}

record_notification_state() {
  notification_state_value=$1
  notification_state_tmp="$CFG_DIR/.notification_state.$$"
  printf '%s\n' "$notification_state_value" > "$notification_state_tmp" 2>/dev/null || return 1
  chmod 600 "$notification_state_tmp" 2>/dev/null || true
  mv -f "$notification_state_tmp" "$NOTIFICATION_STATE_FILE" 2>/dev/null
}

post_boot_notification() {
  notification_requested=$1
  notification_wait=0
  while [ "$notification_wait" -lt 60 ]; do
    [ "$(getprop sys.boot_completed 2>/dev/null)" = "1" ] && break
    sleep 2
    notification_wait=$((notification_wait + 1))
  done

  notification_result=$(notification_live_result)
  [ -n "$notification_result" ] || notification_result=failure
  if [ "$notification_result" != "$notification_requested" ]; then
    log "notification state refreshed requested=$notification_requested actual=$notification_result"
  fi
  notification_previous=$(cat "$NOTIFICATION_STATE_FILE" 2>/dev/null | tr -d '\r' | head -n 1)
  if [ "$notification_previous" = "$notification_result" ]; then
    log "notification unchanged result=$notification_result"
    return 0
  fi
  # Record the attempted transition before posting so an unavailable
  # notification service does not cause retries every watcher cycle.
  record_notification_state "$notification_result" || true

  notification_body=$(notification_text "$notification_result")
  notification_title='A2H 音乐触感'
  notification_tag=a2h_hook
  notification_tmp="/data/local/tmp/.a2h_notification.$$"

  if /system/bin/cmd notification post -S bigtext -t "$notification_title" "$notification_tag" "$notification_body" > "$notification_tmp" 2>&1; then
    log "boot notification posted result=$notification_result style=bigtext"
    rm -f "$notification_tmp" 2>/dev/null
    return 0
  fi
  log "boot notification bigtext failed; trying plain title"
  cat "$notification_tmp" >> "$LOG" 2>/dev/null

  if /system/bin/cmd notification post -t "$notification_title" "$notification_tag" "$notification_body" > "$notification_tmp" 2>&1; then
    log "boot notification posted result=$notification_result style=plain"
    rm -f "$notification_tmp" 2>/dev/null
    return 0
  fi
  log "boot notification titled form failed; trying minimal form"
  cat "$notification_tmp" >> "$LOG" 2>/dev/null

  if /system/bin/cmd notification post "$notification_tag" "$notification_body" > "$notification_tmp" 2>&1; then
    log "boot notification posted result=$notification_result style=minimal"
    rm -f "$notification_tmp" 2>/dev/null
    return 0
  fi
  log "boot notification FAIL result=$notification_result"
  cat "$notification_tmp" >> "$LOG" 2>/dev/null
  rm -f "$notification_tmp" 2>/dev/null
  return 1
}

set_runtime_status() {
  next_runtime_status=$1
  if [ "$runtime_status" = "$next_runtime_status" ]; then
    notification_recorded=$(cat "$NOTIFICATION_STATE_FILE" 2>/dev/null | tr -d '\r' | head -n 1)
    [ "$notification_recorded" = "$next_runtime_status" ] && return 0
    [ "$(getprop sys.boot_completed 2>/dev/null)" = "1" ] || return 0
    post_boot_notification "$next_runtime_status" &
    return 0
  fi
  previous_runtime_status=${runtime_status:-unknown}
  runtime_status=$next_runtime_status
  log "runtime status transition $previous_runtime_status -> $runtime_status"
  if [ "$(getprop sys.boot_completed 2>/dev/null)" = "1" ]; then
    post_boot_notification "$runtime_status" &
  fi
}

module_version=$(sed -n 's/^version=//p' "$MODDIR/module.prop" 2>/dev/null | head -n 1)
[ -n "$module_version" ] || module_version=unknown
printf '[a2h_hook] %s %s\n' "$module_version" "$(date)" > "$LOG" 2>/dev/null

mkdir -p "$CFG_DIR" /data/local/tmp 2>/dev/null
rm -f "$NOTIFICATION_STATE_FILE" 2>/dev/null
chmod 755 "$APPLIER" "$PATCHER" 2>/dev/null

if [ ! -f "$APPLIER" ] || [ ! -f "$PATCHER" ]; then
  log "boot apply FAIL missing executable applier=$([ -f "$APPLIER" ] && printf yes || printf no) patcher=$([ -f "$PATCHER" ] && printf yes || printf no)"
  post_boot_notification failure &
  exit 1
fi

log "boot auto-apply start"
boot_ok=0
boot_try=1
while [ "$boot_try" -le 30 ]; do
  if apply_once "boot#$boot_try"; then
    boot_ok=1
    break
  fi
  log "boot auto-apply retry=$boot_try"
  sleep 2
  boot_try=$((boot_try + 1))
done

if [ "$boot_ok" = "1" ]; then
  boot_mode=$(cat "$CFG_STATE" 2>/dev/null | tr -d '\r' | head -n 1)
  boot_snapshot=$(cat "$APPLIED_SNAPSHOT" 2>/dev/null)
  log "boot auto-apply verified mode=$boot_mode snapshot=${boot_snapshot:-none} attempts=$boot_try"
  runtime_status=success
  post_boot_notification success &
else
  log "boot auto-apply TIMEOUT attempts=30; watcher will continue recovery"
  runtime_status=failure
  post_boot_notification failure &
fi

last_pid=$(cat "$LAST_PID_FILE" 2>/dev/null)
watch_failures=0
log "watcher start pid=${last_pid:-none}"

while true; do
  sleep 25

  current_snapshot=$(A2H_QUIET_PREPARE=1 sh "$APPLIER" snapshot 2>/dev/null)
  snapshot_rc=$?
  current_pid=$(find_hal_pid)
  applied_snapshot=$(cat "$APPLIED_SNAPSHOT" 2>/dev/null)
  need_apply=0
  apply_reason=

  if [ "$snapshot_rc" -ne 0 ] || [ -z "$current_snapshot" ]; then
    watch_failures=$((watch_failures + 1))
    [ $((watch_failures % 6)) -eq 0 ] && log "watcher config prepare failed x$watch_failures"
    set_runtime_status failure
    continue
  fi

  if [ -z "$current_pid" ]; then
    watch_failures=$((watch_failures + 1))
    [ $((watch_failures % 6)) -eq 0 ] && log "watcher HAL not found x$watch_failures"
    set_runtime_status failure
    continue
  fi

  if [ "$current_pid" != "$last_pid" ]; then
    need_apply=1
    apply_reason="pid-change:${last_pid:-none}-$current_pid"
    log "watcher HAL pid changed ${last_pid:-none} -> $current_pid"
  fi

  if [ "$current_snapshot" != "$applied_snapshot" ]; then
    need_apply=1
    if [ -n "$apply_reason" ]; then
      apply_reason="$apply_reason,config-change"
    else
      apply_reason=config-change
    fi
    log "watcher config changed applied=${applied_snapshot:-none} current=$current_snapshot"
  fi

  if [ "$need_apply" = "0" ]; then
    watch_mode=$(cat "$CFG_STATE" 2>/dev/null | tr -d '\r' | head -n 1)
    watch_want=whitelist
    [ "$watch_mode" = "enabled" ] && watch_want=global
    if ! A2H_QUIET_CHECK=1 A2H_QUIET_PREPARE=1 sh "$APPLIER" check "$watch_want" >/dev/null 2>&1; then
      need_apply=1
      apply_reason=live-check
      log "watcher live check failed pid=$current_pid mode=$watch_want"
    fi
  fi

  if [ "$need_apply" = "1" ]; then
    if apply_once "watch:$apply_reason"; then
      last_pid=$(cat "$LAST_PID_FILE" 2>/dev/null)
      watch_failures=0
      log "watcher apply verified reason=$apply_reason pid=${last_pid:-unknown}"
      set_runtime_status success
    else
      watch_failures=$((watch_failures + 1))
      log "watcher apply FAIL reason=$apply_reason failures=$watch_failures"
      set_runtime_status failure
    fi
  else
    last_pid=$current_pid
    watch_failures=0
    set_runtime_status success
  fi
done
