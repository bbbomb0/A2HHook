#!/system/bin/sh
MODDIR="${MODPATH:-$(cd "$(dirname "$0")" && pwd)}"
PATCHER="$MODDIR/bin/a2h_patch"
CFG_STATE="$MODDIR/config/state"
CFG_PKGS="$MODDIR/config/packages.txt"
TMP_STATE="/data/local/tmp/a2h_state"
TMP_PKGS="/data/local/tmp/a2h_packages.txt"
LOG="$MODDIR/action.log"
MAIN_LOG="$MODDIR/a2h_patch.log"
LOCK_FILE="/data/local/tmp/a2h_apply.lock"

ts() { date '+%F %T'; }
log() {
  msg="[$(ts)] $*"
  echo "$msg" >> "$LOG" 2>/dev/null
  echo "$msg" >> "$MAIN_LOG" 2>/dev/null
}

chmod 755 "$PATCHER" 2>/dev/null
touch "$LOG" "$MAIN_LOG" 2>/dev/null

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

ensure_state_file
ensure_packages_file

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
      if (score > best) { best=score; best_addr="0x" a[1]; best_name=$6; }
    }
    END { if (best_addr) print best_addr; }
  '
}

get_state() {
  if [ -f "$CFG_STATE" ]; then
    cat "$CFG_STATE"
  elif [ -f "$TMP_STATE" ]; then
    cat "$TMP_STATE"
  else
    echo disabled
  fi
}

# Prefer tmp if newer write from WebUI stage-1, else keep module.
sync_package_file() {
  mkdir -p /data/local/tmp "$MODDIR/config" 2>/dev/null
  # WebUI writes TMP first. Prefer TMP when it has content.
  if [ -s "$TMP_PKGS" ]; then
    cp "$TMP_PKGS" "$CFG_PKGS" 2>/dev/null
  elif [ -f "$CFG_PKGS" ]; then
    cp "$CFG_PKGS" "$TMP_PKGS" 2>/dev/null
  else
    ensure_packages_file
    cp "$CFG_PKGS" "$TMP_PKGS" 2>/dev/null
  fi
  # keep both mirrors identical
  if [ -f "$CFG_PKGS" ]; then
    cp "$CFG_PKGS" "$TMP_PKGS" 2>/dev/null
  fi
  # normalize to exactly 10 lines for stable slot mapping
  if [ -f "$CFG_PKGS" ]; then
    tmpn="/data/local/tmp/a2h_packages.norm"
    : > "$tmpn"
    i=0
    while IFS= read -r line || [ -n "$line" ]; do
      [ $i -ge 10 ] && break
      # strip CR
      line=$(printf '%s' "$line" | tr -d '
')
      printf '%s
' "$line" >> "$tmpn"
      i=$((i+1))
    done < "$CFG_PKGS"
    while [ $i -lt 10 ]; do
      printf '
' >> "$tmpn"
      i=$((i+1))
    done
    cp "$tmpn" "$CFG_PKGS" 2>/dev/null
    cp "$tmpn" "$TMP_PKGS" 2>/dev/null
    rm -f "$tmpn" 2>/dev/null
  fi
}

with_lock() {
  if ! mkdir "$LOCK_FILE" 2>/dev/null; then
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

apply_now() {
  state="$1"
  begin=$(date +%s)
  log "action apply start state=$state"
  if ! with_lock; then
    log "action apply busy, wait briefly"
    sleep 1
    if ! with_lock; then
      log "action apply FAIL busy"
      return 1
    fi
  fi
  sync_package_file
  printf '%s\n' "$state" > "$TMP_STATE" 2>/dev/null
  printf '%s\n' "$state" > "$CFG_STATE" 2>/dev/null
  log "packages:"
  if [ -f "$CFG_PKGS" ]; then
    nl -ba "$CFG_PKGS" 2>/dev/null | while read -r line; do log "  $line"; done
  fi
  PID=$(find_hal_pid)
  if [ -z "$PID" ]; then
    log "action apply FAIL no-hal"
    unlock
    return 1
  fi
  BASE=$(find_hal_base "$PID")
  [ -n "$BASE" ] || BASE=$(find_hal_base_generic "$PID")
  BASE_ARG=""
  [ -n "$BASE" ] && BASE_ARG="--base $BASE"
  log "pid=$PID base=${BASE:-auto}"
  rc=1
  if [ "$state" = "enabled" ]; then
    if "$PATCHER" "$PID" $BASE_ARG >> "$MAIN_LOG" 2>&1; then rc=0; fi
  else
    if "$PATCHER" --disable "$PID" "$CFG_PKGS" $BASE_ARG >> "$MAIN_LOG" 2>&1; then rc=0; fi
  fi
  end=$(date +%s)
  log "action apply done rc=$rc elapsed=$((end-begin))s"
  unlock
  return $rc
}

queue_apply() {
  state="$1"
  [ "$state" = "enabled" ] || state="disabled"
  sync_package_file
  printf '%s\n' "$state" > "$TMP_STATE" 2>/dev/null
  printf '%s\n' "$state" > "$CFG_STATE" 2>/dev/null
  log "action queue state=$state"
  cmd="disable"
  [ "$state" = "enabled" ] && cmd="enable"
  if command -v nohup >/dev/null 2>&1; then
    nohup sh "$MODDIR/action.sh" "$cmd" >/dev/null 2>&1 < /dev/null &
  else
    sh "$MODDIR/action.sh" "$cmd" >/dev/null 2>&1 < /dev/null &
  fi
  echo "QUEUED $state"
  return 0
}

cmd="${1:-}"
case "$cmd" in
  enable|on|global)
    apply_now enabled
    exit $?
    ;;
  disable|off|whitelist|list)
    apply_now disabled
    exit $?
    ;;
  queue|async)
    st="${2:-$(get_state)}"
    [ "$st" = "enabled" ] || st="disabled"
    queue_apply "$st"
    exit $?
    ;;
  show|status)
    ensure_state_file
    ensure_packages_file
    sync_package_file
    PID=$(find_hal_pid)
    if [ -z "$PID" ]; then
      echo "HAL not found"
      exit 1
    fi
    BASE=$(find_hal_base "$PID")
    [ -n "$BASE" ] || BASE=$(find_hal_base_generic "$PID")
    BASE_ARG=""
    [ -n "$BASE" ] && BASE_ARG="--base $BASE"
    "$PATCHER" --show "$PID" $BASE_ARG
    exit $?
    ;;
  apply|"")
    st=$(get_state)
    [ "$st" = "enabled" ] || st="disabled"
    apply_now "$st"
    exit $?
    ;;
  *)
    echo "usage: action.sh [enable|disable|queue|show|apply]"
    exit 1
    ;;
esac
