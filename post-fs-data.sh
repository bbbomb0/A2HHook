#!/system/bin/sh
# Remove only this module's retired audio-service wrapper property. The native
# patcher in service.sh is the single runtime path in v1.5.5-fix.
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
