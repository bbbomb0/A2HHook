#!/system/bin/sh
# Fast log share for A2HHook WebUI.
# cancel => keep zip; send success => delete zip.
# No large helper binary. Prefer Documents URI to avoid slow MediaStore roundtrips.
MODDIR="${0%/*}"
LOG1="$MODDIR/a2h_patch.log"
LOG2="$MODDIR/action.log"
OUT_DIR="/storage/emulated/0/Download"
TS=$(date +%Y%m%d_%H%M%S 2>/dev/null || date +%s)
NAME="a2h_hook_logs_${TS}.zip"
OUT="$OUT_DIR/$NAME"
TMP="/data/local/tmp/a2h_share_$$"
WATCH_LOG="/data/local/tmp/a2h_share_watch.log"
MAX_LOG_BYTES="${A2H_SHARE_LOG_MAX:-262144}"

mkdir -p "$OUT_DIR" "$TMP" 2>/dev/null

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

# Documents URI is readable and avoids multi-second MediaStore content calls on HyperOS.
URI="content://com.android.externalstorage.documents/document/primary%3ADownload%2F${NAME}"

if am start --user 0 \
    -a android.intent.action.SEND \
    -t application/zip \
    -d "$URI" \
    --eu android.intent.extra.STREAM "$URI" \
    --es android.intent.extra.SUBJECT "A2HHook logs" \
    --grant-read-uri-permission \
    --grant-prefix-uri-permission \
    -f 0x10000001 >/dev/null 2>&1; then
  echo "SHARE_OK doc $URI"
  echo "ZIP_PATH $OUT"
else
  # One-shot MediaStore fallback only if documents share fails.
  if command -v content >/dev/null 2>&1; then
    INS=$(content insert --uri content://media/external/file \
      --bind _data:s:"$OUT" \
      --bind _display_name:s:"$NAME" \
      --bind mime_type:s:application/zip 2>/dev/null | tr -d '\r')
    [ -z "$INS" ] && {
      MID=$(content query --uri content://media/external/file --projection _id --where "_display_name='$NAME'" 2>/dev/null | sed -n 's/.*_id=\([0-9][0-9]*\).*/\1/p' | head -n1)
      [ -n "$MID" ] && INS="content://media/external/file/$MID"
    }
    if [ -n "$INS" ] && am start --user 0 \
        -a android.intent.action.SEND \
        -t application/zip \
        -d "$INS" \
        --eu android.intent.extra.STREAM "$INS" \
        --grant-read-uri-permission \
        --grant-prefix-uri-permission \
        -f 0x10000001 >/dev/null 2>&1; then
      URI="$INS"
      echo "SHARE_OK content $URI"
      echo "ZIP_PATH $OUT"
    else
      echo "SHARE_FALLBACK $OUT"
      echo "ZIP_PATH $OUT"
    fi
  else
    echo "SHARE_FALLBACK $OUT"
    echo "ZIP_PATH $OUT"
  fi
fi

WATCHER="/data/local/tmp/a2h_share_watch_$$.sh"
cat > "$WATCHER" <<'WATCH'
#!/system/bin/sh
OUT="$1"
URI="$2"
WATCH_LOG="/data/local/tmp/a2h_share_watch.log"
TARGETS='com\.tencent\.mobileqq|com\.tencent\.tim|com\.tencent\.mm|com\.tencent\.wework|com\.alibaba\.android\.rimet|com\.ss\.android\.ugc\.aweme|com\.ss\.android\.lark|com\.android\.bluetooth|com\.google\.android\.apps\.docs|com\.tencent\.qqmini|com\.tencent\.qqlite|bin\.mt\.plus|com\.baidu\.netdisk|com\.chinamobile\.mcloud|com\.termux|com\.google\.android\.gm'
MANAGER='com\.resukisu\.resukisu|kernelsu|ksu\.|manager|WebUI|a2h_hook|com\.android\.settings|com\.miui\.home|com\.android\.launcher|launcher'
CHOOSER='IntentResolver|ChooserActivity|resolver\.ResolverActivity|intentresolver|MiuiResolver|ShareActivity|ResolverActivity'
read_focus() {
  top=$(dumpsys window windows 2>/dev/null | tr -d '\r' | grep -E "mCurrentFocus|mFocusedApp|topResumedActivity|mTopFocusedDisplayId" | head -n 8)
  [ -n "$top" ] || top=$(dumpsys activity activities 2>/dev/null | tr -d '\r' | grep -E "topResumedActivity|mResumedActivity|mFocusedApp|mCurrentFocus|ResumedActivity|ResolverActivity|JumpActivity|qfileJumpActivity|QfavJumpActivity|QlinkShareJumpActivity|SelectConversationUI|SendAppMessageWrapperUI" | head -n 8)
  printf '%s\n' "$top"
}
found=0; saw_chooser=0; i=0
while [ $i -lt 12 ]; do
  top=$(read_focus)
  echo "$top" | grep -Eq "$CHOOSER" && saw_chooser=1
  if echo "$top" | grep -Eq "$TARGETS" && ! echo "$top" | grep -Eq "$MANAGER"; then
    found=1; echo "HIT focus i=$i" >> "$WATCH_LOG" 2>/dev/null; break
  fi
  if [ "$saw_chooser" = "1" ] && [ $i -ge 2 ] && ! echo "$top" | grep -Eq "$CHOOSER"; then
    if echo "$top" | grep -Eq "$TARGETS"; then
      found=1; echo "HIT post-chooser i=$i" >> "$WATCH_LOG" 2>/dev/null; break
    else
      found=0; echo "CANCEL no-target i=$i" >> "$WATCH_LOG" 2>/dev/null; break
    fi
  fi
  [ $i -ge 8 ] && [ "$saw_chooser" = "0" ] && { echo "TIMEOUT no-chooser i=$i" >> "$WATCH_LOG" 2>/dev/null; break; }
  i=$((i+1)); sleep 1
done
if [ "$found" = "1" ]; then
  sleep 1
  bn=$(basename "$OUT" 2>/dev/null)
  rm -f "$OUT" "/sdcard/Download/$bn" "/storage/emulated/0/Download/$bn" 2>/dev/null
  [ -n "$bn" ] && command -v content >/dev/null 2>&1 && content delete --uri content://media/external/file --where "_display_name='$bn'" >/dev/null 2>&1 || true
  echo "CLEANED $OUT uri=$URI" >> "$WATCH_LOG" 2>/dev/null
else
  echo "KEEP $OUT uri=$URI" >> "$WATCH_LOG" 2>/dev/null
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
