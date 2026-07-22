#!/system/bin/sh
# KSU early-init preload for the actual MediaTek audio HAL service.
MODDIR=${0%/*}
REAL=/vendor/bin/hw/android.hardware.audio.service-aidl.mediatek
PRELOAD="$MODDIR/zygisk/arm64-v8a/a2h_hook.so"

# The ptrace patcher remains the primary path. Only enable preload wrapping
# when both the exact service binary and preload library are available.
if [ -x "$REAL" ] && [ -f "$PRELOAD" ] && [ -x "$MODDIR/wrapper.sh" ]; then
  if command -v resetprop >/dev/null 2>&1; then
    resetprop -n wrap.vendor.audio-hal-aidl "$MODDIR/wrapper.sh"
  else
    setprop wrap.vendor.audio-hal-aidl "$MODDIR/wrapper.sh"
  fi
fi
