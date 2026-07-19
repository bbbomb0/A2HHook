#!/system/bin/sh
# KSU early-init preload for the actual MediaTek audio HAL service.
MODDIR=${0%/*}
if command -v resetprop >/dev/null 2>&1; then
  resetprop -n wrap.vendor.audio-hal-aidl "$MODDIR/wrapper.sh"
else
  setprop wrap.vendor.audio-hal-aidl "$MODDIR/wrapper.sh"
fi
