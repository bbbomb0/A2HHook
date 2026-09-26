#!/system/bin/sh
# Install helper for KernelSU / Magisk-compatible managers.
# Repair Windows-backslash zip entries and ensure WebUI/bin paths are correct.

ui_print() {
  echo "$1"
}

MODDIR="${MODPATH:-$MODDIR}"
[ -n "$MODDIR" ] || MODDIR="${0%/*}"

# Cancel an uninstall task left by a previously removed copy before installing
# the replacement companion APK. The task also self-cancels when this module is
# present, but removing it here closes the race before Package Manager work.
rm -f \
  /data/adb/service.d/a2h_hook_companion_cleanup.sh \
  /data/adb/service.d/.a2h_hook_companion_cleanup.* 2>/dev/null

# Only remove the exact historical UTF-8 BOM module id. Broad name matching can
# delete unrelated audio or haptic modules owned by the user.
BOM_PREFIX=$(printf '\357\273\277')
for root in /data/adb/modules /data/adb/modules_update; do
  broken="$root/${BOM_PREFIX}a2h_hook"
  if [ -d "$broken" ] && [ "$broken" != "$MODDIR" ]; then
    ui_print "- 清理早期 BOM 残留模块"
    rm -rf "$broken" 2>/dev/null
  fi
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
repair_backslash_entry "$MODDIR/bin\\a2h_audio_watch"
repair_backslash_entry "$MODDIR/bin\\a2h_inject"
repair_backslash_entry "$MODDIR/config\\packages.txt"
repair_backslash_entry "$MODDIR/config\\state"
repair_backslash_entry "$MODDIR/config\\game_auto_pause"
repair_backslash_entry "$MODDIR/config\\log_enabled"
repair_backslash_entry "$MODDIR/companion\\a2h_companion.apk"

mkdir -p \
  "$MODDIR/bin" \
  "$MODDIR/config" \
  "$MODDIR/webroot" 2>/dev/null

# Preserve user configuration when the manager installs into modules_update.
OLD_MODULE=/data/adb/modules/a2h_hook
old_packages_present=0
old_states_present=0
if [ "$MODDIR" != "$OLD_MODULE" ] && [ -d "$OLD_MODULE/config" ]; then
  [ -f "$OLD_MODULE/config/packages.txt" ] && old_packages_present=1
  [ -f "$OLD_MODULE/config/package_states" ] && old_states_present=1
  for name in state game_auto_pause log_enabled packages.txt package_states config_generation .package_baseline; do
    [ -f "$OLD_MODULE/config/$name" ] || continue
    cp -f "$OLD_MODULE/config/$name" "$MODDIR/config/$name" 2>/dev/null
  done
  # The ZIP contains a default state file. Remove that placeholder when an
  # older installation has package names but no state file, so migration below
  # can derive enabled flags from the old ten-line list.
  if [ "$old_packages_present" -eq 1 ] && [ "$old_states_present" -eq 0 ]; then
    rm -f "$MODDIR/config/package_states" 2>/dev/null
  fi
fi

# A root-level action.sh creates the manager's Action button. Current releases use an
# internal command instead, so remove the legacy entry from both update paths.
rm -f "$MODDIR/action.sh" 2>/dev/null
rm -f "$MODDIR/share_logs.sh" 2>/dev/null

# Current releases use only the native patcher. Remove exact files from this
# module's retired preload/inject paths; never use broad audio-module matches.
for legacy_module in "$MODDIR" /data/adb/modules/a2h_hook /data/adb/modules_update/a2h_hook; do
  [ -d "$legacy_module" ] || continue
  rm -f \
    "$legacy_module/bin/a2h_inject" \
    "$legacy_module/zygisk/arm64-v8a.so" \
    "$legacy_module/zygisk/arm64-v8a/a2h_hook.so" \
    "$legacy_module/zygisk/libc++_shared.so" \
    "$legacy_module/system/lib64/liba2h_hook.so" \
    "$legacy_module/system/lib64/libc++_shared.so" 2>/dev/null
done

wrap_prop=wrap.vendor.audio-hal-aidl
current_wrap=$(getprop "$wrap_prop" 2>/dev/null)
case "$current_wrap" in
  /data/adb/modules/a2h_hook/wrapper.sh|/data/adb/modules_update/a2h_hook/wrapper.sh|"$MODDIR/wrapper.sh")
    if command -v resetprop >/dev/null 2>&1; then
      resetprop --delete "$wrap_prop" 2>/dev/null || resetprop -n "$wrap_prop" '' 2>/dev/null
    else
      setprop "$wrap_prop" '' 2>/dev/null
    fi
    ;;
esac

for legacy_module in /data/adb/modules/a2h_hook /data/adb/modules_update/a2h_hook; do
  [ "$legacy_module" = "$MODDIR" ] && continue
  [ -d "$legacy_module" ] || continue
  rm -f "$legacy_module/action.sh" 2>/dev/null
done

if [ ! -f "$MODDIR/config/state" ]; then
  printf '%s\n' disabled > "$MODDIR/config/state"
fi
if [ ! -f "$MODDIR/config/game_auto_pause" ]; then
  printf '%s\n' enabled > "$MODDIR/config/game_auto_pause"
fi
if [ ! -f "$MODDIR/config/log_enabled" ]; then
  printf '%s\n' enabled > "$MODDIR/config/log_enabled"
fi

packages_preexisting=0
if [ -f "$MODDIR/config/packages.txt" ]; then
  packages_preexisting=1
else
  packages_tmp="$MODDIR/config/.packages.install.$$"
  cat > "$packages_tmp" <<'EOF'
com.kugou.android
com.tencent.qqmusic
com.netease.cloudmusic
cn.kuwo.player
com.miui.player
com.luna.music




EOF
  chmod 0644 "$packages_tmp" 2>/dev/null
  mv -f "$packages_tmp" "$MODDIR/config/packages.txt" 2>/dev/null || rm -f "$packages_tmp" 2>/dev/null
fi

if [ ! -f "$MODDIR/config/package_states" ]; then
  states_tmp="$MODDIR/config/.package_states.install.$$"
  rm -f "$states_tmp" 2>/dev/null
  : > "$states_tmp"
  states_ok=1
  state_count=0
  package_nonempty=0
  if [ "$packages_preexisting" -eq 0 ]; then
    printf '%s\n' 1 1 1 1 1 1 0 0 0 0 > "$states_tmp"
    state_count=10
  else
    while IFS= read -r package_line || [ -n "$package_line" ]; do
      state_count=$((state_count + 1))
      if [ "$state_count" -gt 10 ]; then
        states_ok=0
        break
      fi
      package_line=$(printf '%s' "$package_line" | tr -d '\r')
      if [ -n "$package_line" ]; then
        package_nonempty=1
        printf '1\n' >> "$states_tmp"
      else
        printf '0\n' >> "$states_tmp"
      fi
    done < "$MODDIR/config/packages.txt"
    while [ "$state_count" -lt 10 ] && [ "$states_ok" -eq 1 ]; do
      printf '0\n' >> "$states_tmp"
      state_count=$((state_count + 1))
    done
    # An entirely empty legacy list is usually a damaged/placeholder config;
    # leave states absent so a2h_apply can restore the official defaults.
    [ "$package_nonempty" -eq 1 ] || states_ok=0
  fi
  if [ "$states_ok" -eq 1 ] && [ "$state_count" -eq 10 ]; then
    chmod 0644 "$states_tmp" 2>/dev/null
    mv -f "$states_tmp" "$MODDIR/config/package_states" 2>/dev/null || rm -f "$states_tmp" 2>/dev/null
  else
    # Leave an overlong/corrupt legacy list for a2h_apply's single recovery path.
    rm -f "$states_tmp" 2>/dev/null
  fi
fi

# Strip UTF-8 BOM from critical text files if any
for f in "$MODDIR/module.prop" "$MODDIR/config/packages.txt" "$MODDIR/config/package_states" "$MODDIR/config/config_generation" "$MODDIR/config/.package_baseline" "$MODDIR/config/state" "$MODDIR/config/game_auto_pause" "$MODDIR/config/log_enabled" "$MODDIR/webroot/index.html"; do
  [ -f "$f" ] || continue
  # remove BOM if present
  if [ "$(dd if="$f" bs=1 count=3 2>/dev/null | od -An -tx1 | tr -d ' \n')" = "efbbbf" ]; then
    tail -c +4 "$f" > "$f.nobom" 2>/dev/null && mv "$f.nobom" "$f" 2>/dev/null
  fi
done

# Preserve a valid migrated baseline, or build one when the table is already
# complete. This closes the window before the first service normalize.
generation_value=$(cat "$MODDIR/config/config_generation" 2>/dev/null | tr -d '\r' | head -n 1)
case "$generation_value" in
  ''|*[!0-9]*)
    generation_value=$(sed -n '12s/^generation=//p' "$MODDIR/config/.package_baseline" 2>/dev/null)
    case "$generation_value" in
      ''|*[!0-9]*) generation_value=0 ;;
    esac
    ;;
esac
generation_tmp="$MODDIR/config/.generation.install.$$"
printf '%s\n' "$generation_value" > "$generation_tmp" 2>/dev/null &&
  chmod 0644 "$generation_tmp" 2>/dev/null &&
  mv -f "$generation_tmp" "$MODDIR/config/config_generation" 2>/dev/null
rm -f "$generation_tmp" 2>/dev/null

baseline_tmp="$MODDIR/config/.package_baseline.install.$$"
rm -f "$baseline_tmp" 2>/dev/null
baseline_valid=0
if [ -f "$MODDIR/config/.package_baseline" ] &&
   [ "$(wc -l < "$MODDIR/config/.package_baseline" 2>/dev/null | tr -d ' ')" = "12" ]; then
  baseline_state=$(sed -n '11s/^states=//p' "$MODDIR/config/.package_baseline" 2>/dev/null)
  baseline_generation=$(sed -n '12s/^generation=//p' "$MODDIR/config/.package_baseline" 2>/dev/null)
  case "$baseline_state" in
    *:*) ;;
    *) baseline_state=invalid ;;
  esac
  case "$baseline_generation" in
    ''|*[!0-9]*) baseline_generation=invalid ;;
  esac
  if [ "$baseline_state" != "invalid" ] && [ "$baseline_generation" != "invalid" ]; then
    baseline_valid=1
  fi
fi
if [ "$baseline_valid" = "0" ] &&
   [ "$(wc -l < "$MODDIR/config/packages.txt" 2>/dev/null | tr -d ' ')" = "10" ] &&
   [ "$(wc -l < "$MODDIR/config/package_states" 2>/dev/null | tr -d ' ')" = "10" ] &&
   awk '($0 != "0" && $0 != "1") { bad=1 } END { exit bad ? 1 : 0 }' "$MODDIR/config/package_states" 2>/dev/null; then
  install_state_signature=$(cksum < "$MODDIR/config/package_states" 2>/dev/null | awk '{print $1 ":" $2}')
  {
    sed -n '1,10p' "$MODDIR/config/packages.txt"
    printf 'states=%s\n' "$install_state_signature"
    printf 'generation=%s\n' "$generation_value"
  } > "$baseline_tmp" 2>/dev/null &&
    chmod 0600 "$baseline_tmp" 2>/dev/null &&
    mv -f "$baseline_tmp" "$MODDIR/config/.package_baseline" 2>/dev/null
fi
rm -f "$baseline_tmp" 2>/dev/null

chmod 755 \
  "$MODDIR/bin/a2h_patch" \
  "$MODDIR/bin/a2h_trigger" \
  "$MODDIR/bin/a2h_audio_watch" \
  "$MODDIR/bin/a2h_apply" \
  "$MODDIR/service.sh" \
  "$MODDIR/post-fs-data.sh" \
  "$MODDIR/wrapper.sh" \
  "$MODDIR/uninstall.sh" \
  "$MODDIR/customize.sh" 2>/dev/null

chmod 644 \
  "$MODDIR/LICENSE" \
  "$MODDIR/module.prop" \
  "$MODDIR/webui.png" \
  "$MODDIR/companion/a2h_companion.apk" \
  "$MODDIR/config/packages.txt" \
  "$MODDIR/config/package_states" \
  "$MODDIR/config/config_generation" \
  "$MODDIR/config/state" \
  "$MODDIR/config/game_auto_pause" \
  "$MODDIR/config/log_enabled" \
  "$MODDIR/webroot/index.html" \
  "$MODDIR/webroot/coolapk.webp" 2>/dev/null

chmod 600 "$MODDIR/config/.package_baseline" 2>/dev/null

companion_apk="$MODDIR/companion/a2h_companion.apk"
if [ -f "$companion_apk" ] && command -v pm >/dev/null 2>&1; then
  companion_package=io.github.bbbomb0.a2hhook
  companion_expected_code=1595
  companion_expected_hash=
  companion_installed_hash=
  companion_installed_code=$(dumpsys package "$companion_package" 2>/dev/null |
    sed -n 's/^[[:space:]]*versionCode=\([0-9][0-9]*\).*/\1/p' | head -n 1)
  companion_installed_apk=$(pm path "$companion_package" 2>/dev/null |
    sed -n 's/^package://p' | head -n 1)
  if command -v sha256sum >/dev/null 2>&1; then
    companion_expected_hash=$(sha256sum "$companion_apk" 2>/dev/null | awk '{print $1}')
    [ -z "$companion_installed_apk" ] ||
      companion_installed_hash=$(sha256sum "$companion_installed_apk" 2>/dev/null | awk '{print $1}')
  fi
  if [ -n "$companion_installed_apk" ]; then
    case "$companion_installed_code" in
      ''|*[!0-9]*) companion_action=replace ;;
      *)
        if [ "$companion_installed_code" -gt "$companion_expected_code" ]; then
          companion_action=keep
        elif [ "$companion_installed_code" -eq "$companion_expected_code" ] &&
             [ -n "$companion_expected_hash" ] &&
             [ "$companion_installed_hash" = "$companion_expected_hash" ]; then
          companion_action=keep
        else
          companion_action=replace
        fi
        ;;
    esac
  else
    companion_action=install
  fi
  if [ "$companion_action" = keep ]; then
    ui_print "- 控制中心组件已是相同内容或更新版本，保留现有数据与 Root 授权"
  else
    if [ "$companion_action" = replace ]; then
      companion_result=$(pm install --user 0 -r "$companion_apk" 2>&1)
    else
      companion_result=$(pm install --user 0 "$companion_apk" 2>&1)
    fi
    if [ "$?" -eq 0 ]; then
      if [ "$companion_action" = replace ]; then
        ui_print "- 控制中心组件已保留数据覆盖升级"
      else
        ui_print "- 控制中心组件已首次安装"
      fi
    else
      ui_print "! 磁贴组件安装失败，核心音乐触感模块不受影响"
      ui_print "! 已安全保留原 APK；若签名不一致，请安装同签名版本"
      ui_print "! $companion_result"
    fi
  fi
else
  ui_print "! 磁贴组件缺失或系统安装服务不可用"
fi

if [ -f "$MODDIR/webroot/index.html" ]; then
  ui_print "- WebUI 已就绪，请在 KernelSU / 模块栏直接打开"
else
  ui_print "! WebUI 缺失: webroot/index.html"
fi
installed_version=$(sed -n 's/^version=//p' "$MODDIR/module.prop" 2>/dev/null | head -n 1)
[ -n "$installed_version" ] || installed_version=unknown
ui_print "- A2HHook $installed_version 安装完成"
