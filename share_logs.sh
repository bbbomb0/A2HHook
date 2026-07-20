#!/system/bin/sh
# Fast log share for A2HHook WebUI.
# cancel => keep zip; send success => delete zip.
# Prefer DocumentsProvider URI for QQ preview compatibility, while keeping
# MediaStore visibility for file managers and fallbacks.
MODDIR="${0%/*}"
LOG1="$MODDIR/a2h_patch.log"
LOG2="$MODDIR/action.log"
MAIN_LOG="$MODDIR/a2h_patch.log"
ACTION_LOG="$MODDIR/action.log"
OUT_DIR="/storage/emulated/0/Download"
TS=$(date +%Y%m%d_%H%M%S 2>/dev/null || date +%s)
NAME="a2h_hook_logs_${TS}.zip"
OUT="$OUT_DIR/$NAME"
TMP="/data/local/tmp/a2h_share_$$"
WATCH_LOG="/data/local/tmp/a2h_share_watch.log"
MAX_LOG_BYTES="${A2H_SHARE_LOG_MAX:-262144}"

mkdir -p "$OUT_DIR" "$TMP" 2>/dev/null

log_share() {
  msg="[$(date '+%F %T')] [share] $*"
  echo "$msg" >> "$MAIN_LOG" 2>/dev/null
  echo "$msg" >> "$ACTION_LOG" 2>/dev/null
  echo "$msg" >> "$WATCH_LOG" 2>/dev/null
}

extract_uri() {
  printf '%s' "$1" | sed -n 's/.*\(content:\/\/[^ ]*\).*/\1/p' | tail -n1
}

copy_log_tail() {
  src="$1"
  dst="$2"
  [ -f "$src" ] || return 1
  sz=$(wc -c < "$src" 2>/dev/null | tr -d ' \r\n')
  [ -n "$sz" ] || sz=0
  if [ "$sz" -gt "$MAX_LOG_BYTES" ] 2>/dev/null; then
    {
      printf '[a2h_hook] log truncated for quick share: last %s bytes of %s\n' "$MAX_LOG_BYTES" "$src"
      tail -c "$MAX_LOG_BYTES" "$src" 2>/dev/null || tail -n 400 "$src" 2>/dev/null || cat "$src" 2>/dev/null
    } > "$dst"
  else
    cp "$src" "$dst" 2>/dev/null
  fi
}

copy_log_tail "$LOG1" "$TMP/a2h_patch.log" 2>/dev/null || true
copy_log_tail "$LOG2" "$TMP/action.log" 2>/dev/null || true
[ -s "$TMP/a2h_patch.log" ] || printf '%s\n' "[a2h_hook] a2h_patch.log empty or missing @ $TS" > "$TMP/a2h_patch.log"
[ -s "$TMP/action.log" ] || printf '%s\n' "[a2h_hook] action.log empty or missing @ $TS" > "$TMP/action.log"

u32(){ v=$(($1+0)); printf "\\$(printf '%03o' $((v&255)))\\$(printf '%03o' $(((v>>8)&255)))\\$(printf '%03o' $(((v>>16)&255)))\\$(printf '%03o' $(((v>>24)&255)))"; }
u16(){ v=$(($1+0)); printf "\\$(printf '%03o' $((v&255)))\\$(printf '%03o' $(((v>>8)&255)))"; }

add_entry() {
  zf="$1"; path="$2"; name="$3"
  sz=$(wc -c < "$path" 2>/dev/null | tr -d ' \r\n')
  [ -n "$sz" ] || sz=0
  crc=$(cksum -HPLN "$path" 2>/dev/null | awk '{print "0x"$1}')
  [ -n "$crc" ] || crc=0
  off=$(wc -c < "$zf" 2>/dev/null | tr -d ' \r\n')
  [ -n "$off" ] || off=0
  nlen=${#name}
  {
    u32 0x04034b50; u16 20; u16 0; u16 0; u16 0; u16 0
    u32 "$crc"; u32 "$sz"; u32 "$sz"; u16 "$nlen"; u16 0
    printf '%s' "$name"
    cat "$path"
  } >> "$zf"
  printf '%s %s %s %s\n' "$name" "$crc" "$sz" "$off" >> "$TMP/meta"
}

make_zip() {
  rm -f "$OUT" "$TMP/meta" 2>/dev/null
  : > "$OUT"
  : > "$TMP/meta"
  add_entry "$OUT" "$TMP/a2h_patch.log" "a2h_patch.log" || return 1
  add_entry "$OUT" "$TMP/action.log" "action.log" || return 1
  cd_off=$(wc -c < "$OUT" 2>/dev/null | tr -d ' \r\n')
  n=0
  while read -r name crc sz off; do
    [ -n "$name" ] || continue
    nlen=${#name}
    {
      u32 0x02014b50; u16 20; u16 20; u16 0; u16 0; u16 0; u16 0
      u32 "$crc"; u32 "$sz"; u32 "$sz"; u16 "$nlen"; u16 0; u16 0; u16 0; u16 0; u32 0; u32 "$off"
      printf '%s' "$name"
    } >> "$OUT"
    n=$((n+1))
  done < "$TMP/meta"
  cd_end=$(wc -c < "$OUT" 2>/dev/null | tr -d ' \r\n')
  cd_sz=$((cd_end-cd_off))
  { u32 0x06054b50; u16 0; u16 0; u16 "$n"; u16 "$n"; u32 "$cd_sz"; u32 "$cd_off"; u16 0; } >> "$OUT"
  [ -s "$OUT" ]
}

if ! make_zip; then
  echo "ZIP_FAIL"
  rm -rf "$TMP" 2>/dev/null
  exit 2
fi

chmod 644 "$OUT" 2>/dev/null
chown media_rw:media_rw "$OUT" 2>/dev/null || true

if [ "${A2H_SHARE_DRYRUN:-0}" = "1" ]; then
  echo "DRYRUN ZIP_PATH $OUT"
  rm -rf "$TMP" 2>/dev/null
  exit 0
fi

DOC_URI="content://com.android.externalstorage.documents/document/primary%3ADownload%2F${NAME}"
MEDIA_URI=""
if command -v content >/dev/null 2>&1; then
  INS=$(content insert --uri content://media/external/downloads \
    --bind _data:s:"$OUT" \
    --bind _display_name:s:"$NAME" \
    --bind mime_type:s:application/zip 2>/dev/null | tr -d '\r')
  case "$INS" in
    *content://*)
      MEDIA_URI=$(extract_uri "$INS")
      ;;
  esac
  if [ -z "$MEDIA_URI" ]; then
    MID=$(content query --uri content://media/external/downloads --projection _id --where "_display_name='$NAME'" 2>/dev/null | sed -n 's/.*_id=\([0-9][0-9]*\).*/\1/p' | head -n1)
    [ -n "$MID" ] && MEDIA_URI="content://media/external/downloads/$MID"
  fi
fi
URI="$DOC_URI"
log_share "share start uri=$URI media_uri=${MEDIA_URI:-none} out=$OUT"

start_share() {
  share_uri="$1"
  am start --user 0 \
    -a android.intent.action.SEND \
    -c android.intent.category.DEFAULT \
    -t application/zip \
    -d "$share_uri" \
    --eu android.intent.extra.STREAM "$share_uri" \
    --es android.intent.extra.TITLE "$NAME" \
    --es android.intent.extra.SUBJECT "A2HHook logs" \
    --grant-read-uri-permission \
    --grant-prefix-uri-permission \
    -f 0x10000001 >/dev/null 2>&1
}

if start_share "$URI"; then
  echo "SHARE_OK $URI"
  echo "ZIP_PATH $OUT"
  log_share "share intent ok uri=$URI"
else
  # MediaStore fallback for ROMs/apps that reject ExternalStorageProvider URI.
  if [ -n "$MEDIA_URI" ] && start_share "$MEDIA_URI"; then
    URI="$MEDIA_URI"
    echo "SHARE_OK $URI"
    echo "ZIP_PATH $OUT"
    log_share "share fallback intent ok uri=$URI"
  else
    echo "SHARE_FALLBACK $OUT"
    echo "ZIP_PATH $OUT"
    log_share "share fallback keep uri=$URI media_uri=${MEDIA_URI:-none}"
  fi
fi

WATCHER="/data/local/tmp/a2h_share_watch_$$.sh"
cat > "$WATCHER" <<'WATCH'
#!/system/bin/sh
OUT="$1"
URI="$2"
WATCH_LOG="/data/local/tmp/a2h_share_watch.log"
MAIN_LOG="/data/adb/modules/a2h_hook/a2h_patch.log"
ACTION_LOG="/data/adb/modules/a2h_hook/action.log"
TARGETS='com\.tencent\.mobileqq|com\.tencent\.qqmini|com\.tencent\.qqlite|com\.tencent\.tim|com\.tencent\.mm|com\.tencent\.wework|com\.alibaba\.android\.rimet|com\.ss\.android\.ugc\.aweme|com\.ss\.android\.lark|com\.android\.bluetooth|com\.google\.android\.apps\.docs|bin\.mt\.plus|com\.baidu\.netdisk|com\.chinamobile\.mcloud|com\.termux|com\.google\.android\.gm'
MANAGER='com\.resukisu\.resukisu|kernelsu|ksu\.|manager|WebUI|a2h_hook|com\.android\.settings|com\.miui\.home|com\.android\.launcher|launcher'
CHOOSER='IntentResolver|ChooserActivity|resolver\.ResolverActivity|intentresolver|MiuiResolver|ShareActivity|ResolverActivity|com\.android\.intentresolver|com\.miui\.resolver'
SHARE_UI='JumpActivity|qfileJumpActivity|QfavJumpActivity|QlinkShareJumpActivity|Forward|SelectConversation|SendAppMessage|Share|share|Chooser|Resolver|IntentResolver'
MAX_LOOPS="${A2H_SHARE_WATCH_LOOPS:-90}"
INTERVAL="${A2H_SHARE_WATCH_INTERVAL:-2}"
TARGET_CLEAN_LOOPS="${A2H_SHARE_TARGET_CLEAN_LOOPS:-45}"
TARGET_LEFT_LOOPS="${A2H_SHARE_TARGET_LEFT_LOOPS:-2}"
log_w() {
  msg="[$(date '+%F %T')] [share] $*"
  echo "$msg" >> "$WATCH_LOG" 2>/dev/null
  echo "$msg" >> "$MAIN_LOG" 2>/dev/null
  echo "$msg" >> "$ACTION_LOG" 2>/dev/null
}
brief() {
  printf '%s' "$1" | tr '\n' '|' | cut -c 1-360
}
read_focus() {
  {
    dumpsys window windows 2>/dev/null | tr -d '\r' | grep -E "mCurrentFocus|mFocusedApp|topResumedActivity|mTopFocusedDisplayId|com.tencent|Resolver|Chooser|ShareActivity" | head -n 10
    dumpsys activity activities 2>/dev/null | tr -d '\r' | grep -E "topResumedActivity|mResumedActivity|mFocusedApp|mCurrentFocus|ResumedActivity|ResolverActivity|ChooserActivity|JumpActivity|qfileJumpActivity|QfavJumpActivity|QlinkShareJumpActivity|SelectConversation|SendAppMessage|Forward|com.tencent" | head -n 16
    dumpsys activity top 2>/dev/null | tr -d '\r' | grep -E "ACTIVITY|topResumedActivity|mResumedActivity|mFocusedApp|mCurrentFocus|Resolver|Chooser|ShareActivity|JumpActivity|Forward|SelectConversation|SendAppMessage|com.tencent" | head -n 12
  } | head -n 36
}
found=0; saw_chooser=0; target_seen=0; target_first=-1; target_last=-1; keep_reason="timeout"; clean_reason=""; last_brief=""; i=0
while [ "$i" -lt "$MAX_LOOPS" ] 2>/dev/null; do
  top=$(read_focus)
  top_brief=$(brief "$top")
  [ -n "$top_brief" ] && last_brief="$top_brief"
  echo "$top" | grep -Eq "$CHOOSER" && saw_chooser=1
  if echo "$top" | grep -Eq "$TARGETS" && ! echo "$top" | grep -Eq "$MANAGER"; then
    if [ "$target_seen" = "0" ]; then
      target_seen=1
      target_first=$i
      log_w "SEE target i=$i top=$top_brief"
    fi
    target_last=$i
    if [ $((i-target_first)) -ge "$TARGET_CLEAN_LOOPS" ] 2>/dev/null; then
      found=1
      clean_reason="target-safe-delay"
      log_w "HIT $clean_reason i=$i top=$top_brief"
      break
    fi
  else
    if [ "$target_seen" = "1" ] && [ $((i-target_last)) -ge "$TARGET_LEFT_LOOPS" ] 2>/dev/null; then
      found=1
      clean_reason="target-left"
      log_w "HIT $clean_reason i=$i top=$top_brief"
      break
    fi
  fi
  if [ "$saw_chooser" = "1" ] && [ "$target_seen" = "0" ] && [ "$i" -ge 6 ] && ! echo "$top" | grep -Eq "$CHOOSER|$TARGETS"; then
    keep_reason="chooser-dismissed"
    log_w "KEEP $keep_reason i=$i top=$top_brief"
    break
  fi
  i=$((i+1)); sleep "$INTERVAL"
done
if [ "$found" = "1" ]; then
  sleep "${A2H_SHARE_DELETE_DELAY:-5}"
  bn=$(basename "$OUT" 2>/dev/null)
  rm -f "$OUT" "/sdcard/Download/$bn" "/storage/emulated/0/Download/$bn" 2>/dev/null
  if [ -n "$bn" ] && command -v content >/dev/null 2>&1; then
    content delete --uri content://media/external/downloads --where "_display_name='$bn'" >/dev/null 2>&1 || true
    content delete --uri content://media/external/file --where "_display_name='$bn'" >/dev/null 2>&1 || true
  fi
  log_w "CLEANED reason=$clean_reason $OUT uri=$URI"
else
  log_w "KEEP reason=$keep_reason chooser=$saw_chooser target=$target_seen i=$i last=$last_brief $OUT uri=$URI"
fi
rm -f "$0" 2>/dev/null
WATCH
chmod 755 "$WATCHER" 2>/dev/null
if command -v setsid >/dev/null 2>&1; then
  setsid sh "$WATCHER" "$OUT" "$URI" >/dev/null 2>&1 < /dev/null &
else
  nohup sh "$WATCHER" "$OUT" "$URI" >/dev/null 2>&1 < /dev/null &
fi
rm -rf "$TMP" 2>/dev/null
exit 0
