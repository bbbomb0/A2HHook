#!/system/bin/sh

COMPANION_PACKAGE=io.github.bbbomb0.a2hhook
COMPANION_CLEANUP=/data/adb/service.d/a2h_hook_companion_cleanup.sh
COMPANION_UNINSTALL_LOG=/data/adb/a2h_hook_uninstall.log

uninstall_log() {
  printf '[%s] %s\n' "$(date '+%F %T' 2>/dev/null)" "$*" >> "$COMPANION_UNINSTALL_LOG" 2>/dev/null || true
}

uninstall_companion_now() {
  command -v pm >/dev/null 2>&1 || {
    uninstall_log "companion uninstall deferred reason=pm-missing"
    return 1
  }
  uninstall_output=$(pm uninstall "$COMPANION_PACKAGE" 2>&1)
  uninstall_log "companion uninstall all-users output=$uninstall_output"
  if pm path "$COMPANION_PACKAGE" >/dev/null 2>&1; then
    uninstall_output=$(pm uninstall --user 0 "$COMPANION_PACKAGE" 2>&1)
    uninstall_log "companion uninstall user0 output=$uninstall_output"
  fi
  if pm path "$COMPANION_PACKAGE" >/dev/null 2>&1; then
    uninstall_log "companion uninstall failed package=$COMPANION_PACKAGE"
    return 1
  fi
  uninstall_log "companion uninstall complete package=$COMPANION_PACKAGE"
  return 0
}

schedule_companion_cleanup() {
  cleanup_dir=${COMPANION_CLEANUP%/*}
  cleanup_tmp="$cleanup_dir/.a2h_hook_companion_cleanup.$$"
  mkdir -p "$cleanup_dir" 2>/dev/null || return 1
  cat > "$cleanup_tmp" <<'EOF'
#!/system/bin/sh

PACKAGE=io.github.bbbomb0.a2hhook
SELF=/data/adb/service.d/a2h_hook_companion_cleanup.sh
LOG=/data/adb/a2h_hook_uninstall.log

cleanup_log() {
  printf '[%s] %s\n' "$(date '+%F %T' 2>/dev/null)" "$*" >> "$LOG" 2>/dev/null || true
}

# A reinstall supersedes this one-shot task. Both locations are checked because
# KernelSU can stage a replacement module before the next reboot.
if [ -f /data/adb/modules/a2h_hook/module.prop ] ||
   [ -f /data/adb/modules_update/a2h_hook/module.prop ]; then
  rm -f "$SELF" 2>/dev/null
  exit 0
fi

attempt=0
while [ "$attempt" -lt 90 ]; do
  if [ "$(getprop sys.boot_completed 2>/dev/null)" = "1" ] &&
     command -v pm >/dev/null 2>&1; then
    output=$(pm uninstall "$PACKAGE" 2>&1)
    cleanup_log "deferred uninstall all-users output=$output"
    if pm path "$PACKAGE" >/dev/null 2>&1; then
      output=$(pm uninstall --user 0 "$PACKAGE" 2>&1)
      cleanup_log "deferred uninstall user0 output=$output"
    fi
    if ! pm path "$PACKAGE" >/dev/null 2>&1; then
      cleanup_log "deferred uninstall complete package=$PACKAGE"
      rm -f "$SELF" 2>/dev/null
      exit 0
    fi
  fi
  sleep 2
  attempt=$((attempt + 1))
done

# Keep the task for one more boot if the package service stayed unavailable.
cleanup_log "deferred uninstall exhausted package=$PACKAGE"
exit 0
EOF
  chmod 0700 "$cleanup_tmp" 2>/dev/null || {
    rm -f "$cleanup_tmp" 2>/dev/null
    return 1
  }
  mv -f "$cleanup_tmp" "$COMPANION_CLEANUP" 2>/dev/null || {
    rm -f "$cleanup_tmp" 2>/dev/null
    return 1
  }
  uninstall_log "scheduled deferred companion cleanup path=$COMPANION_CLEANUP"
}

if [ "$(getprop sys.boot_completed 2>/dev/null)" = "1" ] &&
   uninstall_companion_now; then
  rm -f "$COMPANION_CLEANUP" 2>/dev/null
else
  schedule_companion_cleanup || true
fi

# Remove only this module's fixed runtime artifacts. KernelSU removes MODPATH
# itself after the hook returns; no user data outside the module is touched.
MODDIR=${MODPATH:-/data/adb/modules/a2h_hook}
rm -f \
  /data/local/tmp/a2h_config.changed \
  /data/local/tmp/a2h_config.wake \
  /data/local/tmp/a2h_apply.pending \
  /data/local/tmp/a2h_packages.txt \
  /data/local/tmp/a2h_state \
  "$MODDIR/a2h_patch.log" "$MODDIR/action.log" 2>/dev/null
rm -rf \
  /data/local/tmp/a2h_apply.worker \
  /data/local/tmp/a2h_apply.lock \
  /data/local/tmp/a2h_config.lock \
  /data/local/tmp/a2h_hook_runtime \
  "$MODDIR/config/.apply.lock" \
  "$MODDIR/config/.config.lock" \
  "$MODDIR/config/.notification_lock" 2>/dev/null
