#!/system/bin/sh
# Install helper for KernelSU / Magisk-compatible managers.
# Repair Windows-backslash zip entries and ensure WebUI/bin paths are correct.

ui_print() {
  echo "$1"
}

MODDIR="${MODPATH:-$MODDIR}"
[ -n "$MODDIR" ] || MODDIR="${0%/*}"

# Clean historical broken module ids caused by UTF-8 BOM / garbled module.prop
for d in /data/adb/modules/* /data/adb/modules_update/*; do
  [ -d "$d" ] || continue
  base=$(basename "$d")
  case "$base" in
    a2h_hook) continue ;;
    *a2h*|*A2H*|*音乐触感*|*haptic*|*Audio*To*Haptic*)
      # only remove if it is not our current target path
      if [ "$d" != "$MODDIR" ]; then
        # keep if id is exactly a2h_hook after reading prop
        id=$(grep -m1 '^id=' "$d/module.prop" 2>/dev/null | cut -d= -f2- | tr -d '\r')
        if [ "$id" != "a2h_hook" ]; then
          ui_print "- 清理历史残留模块: $base"
          rm -rf "$d" 2>/dev/null
        fi
      fi
      ;;
  esac
done

repair_backslash_entry() {
  src="$1"
  [ -f "$src" ] || return 0
  case "$src" in
    *\\*) ;;
    *) return 0 ;;
  esac
  rel="${src#$MODDIR/}"
  rel_unix=$(printf '%s' "$rel" | tr '\\' '/')
  dest="$MODDIR/$rel_unix"
  mkdir -p "$(dirname "$dest")" 2>/dev/null
  if [ ! -f "$dest" ]; then
    mv "$src" "$dest" 2>/dev/null || cp "$src" "$dest" 2>/dev/null
  fi
  rm -f "$src" 2>/dev/null
}

for f in "$MODDIR"/*; do
  [ -e "$f" ] || continue
  base=$(basename "$f")
  case "$base" in
    *\\*) repair_backslash_entry "$f" ;;
  esac
done

repair_backslash_entry "$MODDIR/webroot\\index.html"
repair_backslash_entry "$MODDIR/bin\\a2h_patch"
repair_backslash_entry "$MODDIR/bin\\a2h_trigger"
repair_backslash_entry "$MODDIR/bin\\a2h_inject"
repair_backslash_entry "$MODDIR/config\\packages.txt"
repair_backslash_entry "$MODDIR/config\\state"
repair_backslash_entry "$MODDIR/zygisk\\arm64-v8a\\a2h_hook.so"

mkdir -p \
  "$MODDIR/bin" \
  "$MODDIR/config" \
  "$MODDIR/webroot" \
  "$MODDIR/zygisk/arm64-v8a" 2>/dev/null

if [ ! -f "$MODDIR/config/state" ]; then
  printf '%s\n' disabled > "$MODDIR/config/state"
fi

if [ ! -f "$MODDIR/config/packages.txt" ]; then
  cat > "$MODDIR/config/packages.txt" <<'EOF'
com.kugou.android
com.tencent.qqmusic
com.netease.cloudmusic
cn.kuwo.player
com.miui.player
com.luna.music




EOF
fi

# Strip UTF-8 BOM from critical text files if any
for f in "$MODDIR/module.prop" "$MODDIR/config/packages.txt" "$MODDIR/config/state" "$MODDIR/webroot/index.html"; do
  [ -f "$f" ] || continue
  # remove BOM if present
  if [ "$(dd if="$f" bs=1 count=3 2>/dev/null | od -An -tx1 | tr -d ' \n')" = "efbbbf" ]; then
    tail -c +4 "$f" > "$f.nobom" 2>/dev/null && mv "$f.nobom" "$f" 2>/dev/null
  fi
done

chmod 755 \
  "$MODDIR/bin/a2h_patch" \
  "$MODDIR/bin/a2h_trigger" \
  "$MODDIR/service.sh" \
  "$MODDIR/action.sh" \
  "$MODDIR/share_logs.sh" \
  "$MODDIR/post-fs-data.sh" \
  "$MODDIR/wrapper.sh" \
  "$MODDIR/customize.sh" 2>/dev/null

chmod 644 \
  "$MODDIR/module.prop" \
  "$MODDIR/webui.png" \
  "$MODDIR/config/packages.txt" \
  "$MODDIR/config/state" \
  "$MODDIR/webroot/index.html" \
  "$MODDIR/zygisk/arm64-v8a/a2h_hook.so" 2>/dev/null

if [ -f "$MODDIR/webroot/index.html" ]; then
  ui_print "- WebUI 已就绪，请在 KernelSU / 模块栏直接打开"
else
  ui_print "! WebUI 缺失: webroot/index.html"
fi
  ui_print "- A2HHook v1.5.2-fix 安装完成"
