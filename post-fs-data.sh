#!/system/bin/sh
# Remove only this module's retired audio-service wrapper property. The native
# patcher in service.sh is the single runtime path in current releases.

cleanup_stale_runtime() {
  runtime_dir=$1
  rm -f \
    "$runtime_dir/a2h_apply.pending" \
    "$runtime_dir/a2h_state" \
    "$runtime_dir/a2h_packages.txt" \
    "$runtime_dir/a2h_config.changed" \
    "$runtime_dir/a2h_config.wake" \
    "$runtime_dir"/a2h_apply.pending.tmp.* \
    "$runtime_dir"/.a2h_state.* \
    "$runtime_dir"/.a2h_packages.* 2>/dev/null || true
  rm -rf \
    "$runtime_dir/a2h_apply.lock" \
    "$runtime_dir/a2h_apply.worker" \
    "$runtime_dir/a2h_config.lock" 2>/dev/null || true
}

# /data/local/tmp survives a normal reboot. Previous-boot PIDs can never own
# these module-specific locks at post-fs-data time, so clear them before the
# current boot can enqueue work. Never run this cleanup after boot completes.
if [ "$(getprop sys.boot_completed 2>/dev/null)" != "1" ]; then
  cleanup_stale_runtime /data/local/tmp
  rm -rf /data/local/tmp/a2h_hook_runtime 2>/dev/null || true
fi

MODDIR=${0%/*}
WRAP_PROP=wrap.vendor.audio-hal-aidl
current_wrap=$(getprop "$WRAP_PROP" 2>/dev/null)

case "$current_wrap" in
  /data/adb/modules/a2h_hook/wrapper.sh|/data/adb/modules_update/a2h_hook/wrapper.sh|"$MODDIR/wrapper.sh")
  if command -v resetprop >/dev/null 2>&1; then
      resetprop --delete "$WRAP_PROP" 2>/dev/null || resetprop -n "$WRAP_PROP" '' 2>/dev/null
  else
      setprop "$WRAP_PROP" '' 2>/dev/null
  fi
    ;;
esac
