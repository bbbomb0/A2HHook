#!/system/bin/sh
MODDIR="${0%/*}"
PATCHER="$MODDIR/bin/a2h_patch"
CFG_STATE="$MODDIR/config/state"
CFG_PKGS="$MODDIR/config/packages.txt"
TMP_STATE="/data/local/tmp/a2h_state"
TMP_PKGS="/data/local/tmp/a2h_packages.txt"
LOG="$MODDIR/a2h_patch.log"
LAST_PID_FILE="$MODDIR/config/last_pid"
LOCK_FILE="/data/local/tmp/a2h_apply.lock"

ts() { date '+%F %T'; }
log() { echo "[$(ts)] $*" >> "$LOG" 2>/dev/null; }

ensure_state_file() {
  mkdir -p "$MODDIR/config" 2>/dev/null
  if [ ! -f "$CFG_STATE" ]; then
    printf '%s\n' disabled > "$CFG_STATE" 2>/dev/null
  fi
}
ensure_packages_file() {
  mkdir -p "$MODDIR/config" 2>/dev/null
  if [ ! -f "$CFG_PKGS" ]; then
    cat > "$CFG_PKGS" <<'EOF'
com.kugou.android
com.tencent.qqmusic
com.netease.cloudmusic
cn.kuwo.player
com.miui.player
com.luna.music




EOF
  fi
}

# Keep module config as source of truth; mirror to /data/local/tmp for WebUI.
sync_runtime_from_module() {
  mkdir -p /data/local/tmp "$MODDIR/config" 2>/dev/null
  # Prefer module persistent config. If WebUI tmp is newer/non-empty, absorb it.
  if [ -s "$TMP_PKGS" ] && [ -f "$CFG_PKGS" ]; then
    # if tmp differs, treat tmp as latest UI write
    if ! cmp -s "$TMP_PKGS" "$CFG_PKGS" 2>/dev/null; then
      cp "$TMP_PKGS" "$CFG_PKGS" 2>/dev/null
    fi
  fi
  [ -f "$CFG_STATE" ] && cp "$CFG_STATE" "$TMP_STATE" 2>/dev/null
  [ -f "$CFG_PKGS" ] && cp "$CFG_PKGS" "$TMP_PKGS" 2>/dev/null
  if [ -s "$TMP_STATE" ]; then
    # absorb tmp state only if valid
    st=$(cat "$TMP_STATE" 2>/dev/null)
    if [ "$st" = "enabled" ] || [ "$st" = "disabled" ]; then
      printf '%s
' "$st" > "$CFG_STATE" 2>/dev/null
    fi
  fi
}

find_hal_pid() {
  pid=$(pidof android.hardware.audio.service-aidl.mediatek 2>/dev/null | awk '{print $1}')
  [ -n "$pid" ] && { echo "$pid"; return 0; }
  pid=$(pgrep -f android.hardware.audio.service-aidl.mediatek 2>/dev/null | head -n1)
  [ -n "$pid" ] && { echo "$pid"; return 0; }
  pid=$(pidof android.hardware.audio.service 2>/dev/null | awk '{print $1}')
  [ -n "$pid" ] && { echo "$pid"; return 0; }
  for f in /proc/[0-9]*/cmdline; do
    [ -r "$f" ] || continue
    pid=${f#/proc/}
    pid=${pid%%/*}
    if grep -a -F -q "android.hardware.audio.service-aidl.mediatek" "$f" 2>/dev/null; then
      echo "$pid"
      return 0
    fi
    if grep -a -F -q "android.hardware.audio.service" "$f" 2>/dev/null ||
       grep -a -F -q "audio.service-aidl" "$f" 2>/dev/null; then
      if grep -a -F -q "audio.primary." "/proc/$pid/maps" 2>/dev/null; then
        echo "$pid"
        return 0
      fi
    fi
  done
  return 1
}

find_hal_base() {
  pid="$1"
  [ -n "$pid" ] || return 1
  /system/bin/cat "/proc/$pid/maps" 2>/dev/null | awk '/audio.primary.mediatek.so|audio.primary.mt6991.so/ && $3 == "00000000" {split($1,a,"-"); print "0x" a[1]; exit}'
}

find_hal_base_generic() {
  pid="$1"
  [ -n "$pid" ] || return 1
  /system/bin/cat "/proc/$pid/maps" 2>/dev/null | awk '
    /audio\.primary\..*\.so/ && $3 == "00000000" {
      split($1,a,"-");
      score=10;
      if ($6 ~ /audio\.primary\.mediatek\.so/) score+=50;
      if ($6 ~ /audio\.primary\.mt6991\.so/) score+=45;
      if ($6 ~ /\/vendor\/lib64\/hw\//) score+=10;
      if ($2 ~ /r-xp/) score+=3;
      if (score > best) { best=score; best_addr="0x" a[1]; }
    }
    END { if (best_addr) print best_addr; }
  '
}

with_lock() {
  # simple non-blocking lock to avoid WebUI + watcher double apply
  if ! mkdir "$LOCK_FILE" 2>/dev/null; then
    # stale lock older than 20s?
    now=$(date +%s)
    mtime=$(stat -c %Y "$LOCK_FILE" 2>/dev/null || echo 0)
    if [ -n "$mtime" ] && [ $((now - mtime)) -gt 20 ]; then
      rmdir "$LOCK_FILE" 2>/dev/null
      mkdir "$LOCK_FILE" 2>/dev/null || return 1
    else
      return 1
    fi
  fi
  return 0
}
unlock() { rmdir "$LOCK_FILE" 2>/dev/null; }

live_ok() {
  pid="$1"
  mode_want="$2"
  want="whitelist"
  [ "$mode_want" = "enabled" ] && want="global"
  # Quiet by default; only append details when check fails.
  tmp="/data/local/tmp/a2h_live_check.$$"
  if "$PATCHER" --check "$want" "$pid" >"$tmp" 2>&1; then
    rm -f "$tmp" 2>/dev/null
    return 0
  fi
  echo "[live_ok FAIL want=$want pid=$pid]" >> "$LOG" 2>/dev/null
  cat "$tmp" >> "$LOG" 2>/dev/null
  rm -f "$tmp" 2>/dev/null
  return 1
}

apply_once() {
  reason="$1"
  if ! with_lock; then
    log "watch: skip apply (busy) reason=$reason"
    return 0
  fi
  sync_runtime_from_module
  ENABLED=$(cat "$CFG_STATE" 2>/dev/null)
  [ "$ENABLED" = "enabled" ] || ENABLED="disabled"
  PID=$(find_hal_pid)
  if [ -z "$PID" ]; then
    log "watch: HAL missing ($reason)"
    unlock
    return 1
  fi
  BASE=$(find_hal_base "$PID")
  [ -n "$BASE" ] || BASE=$(find_hal_base_generic "$PID")
  BASE_ARG=""
  [ -n "$BASE" ] && BASE_ARG="--base $BASE"
  log "watch apply reason=$reason pid=$PID base=${BASE:-auto} mode=$ENABLED"
  rc=1
  if [ "$ENABLED" = "enabled" ]; then
    if "$PATCHER" "$PID" $BASE_ARG >> "$LOG" 2>&1; then
      printf '%s\n' "$PID" > "$LAST_PID_FILE" 2>/dev/null
      log "watch GLOBAL ok pid=$PID"
      rc=0
    fi
  else
    if "$PATCHER" --disable "$PID" "$CFG_PKGS" $BASE_ARG >> "$LOG" 2>&1; then
      printf '%s\n' "$PID" > "$LAST_PID_FILE" 2>/dev/null
      log "watch WHITELIST ok pid=$PID"
      rc=0
    fi
  fi
  [ "$rc" != "0" ] && log "watch apply FAIL reason=$reason pid=$PID"
  unlock
  return $rc
}

echo "[a2h_hook] v1.5.1 $(date)" > "$LOG"
chmod 755 "$PATCHER" 2>/dev/null
ensure_state_file
ensure_packages_file
sync_runtime_from_module

ENABLED=$(cat "$CFG_STATE" 2>/dev/null)
[ "$ENABLED" = "enabled" ] || ENABLED="disabled"
log "boot apply start mode=$ENABLED"
log "packages file:"
nl -ba "$CFG_PKGS" 2>/dev/null | while read -r line; do log "  $line"; done

ok=0
for i in $(seq 1 30); do
  if apply_once "boot#$i"; then ok=1; break; fi
  sleep 2
done
[ "$ok" = "1" ] || log "boot apply TIMEOUT, continue watcher"

log "watcher start"
LAST_PID=$(cat "$LAST_PID_FILE" 2>/dev/null)
FAILS=0
while true; do
  sleep 25
  sync_runtime_from_module
  ENABLED=$(cat "$CFG_STATE" 2>/dev/null)
  [ "$ENABLED" = "enabled" ] || ENABLED="disabled"
  PID=$(find_hal_pid)
  if [ -z "$PID" ]; then
    FAILS=$((FAILS+1))
    [ $((FAILS % 6)) -eq 0 ] && log "watcher: HAL not found (x$FAILS)"
    continue
  fi

  need=0
  if [ "$PID" != "$LAST_PID" ]; then
    need=1
    log "watcher: HAL pid changed ${LAST_PID:-none} -> $PID"
  else
    # quiet check: only reapply if check fails
    if ! live_ok "$PID" "$ENABLED" >/dev/null 2>&1; then
      # live_ok already logged details to a2h_patch.log
      need=1
      log "watcher: live check failed, will reapply mode=$ENABLED pid=$PID"
    fi
  fi

  if [ "$need" = "1" ]; then
    if apply_once "watch"; then
      LAST_PID=$(cat "$LAST_PID_FILE" 2>/dev/null)
      FAILS=0
    else
      FAILS=$((FAILS+1))
    fi
  fi
done
