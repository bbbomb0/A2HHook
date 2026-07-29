#!/system/bin/sh
MODDIR=/data/adb/modules/a2h_hook
REAL="/vendor/bin/hw/android.hardware.audio.service-aidl.mediatek"

# Compatibility pass-through for an old wrap property during an in-place
# update. post-fs-data.sh removes the property on the next boot.
if [ -x "$REAL" ]; then
  exec "$REAL" "$@"
fi

echo "[a2h_hook] audio HAL executable missing: $REAL" >> "$MODDIR/a2h_patch.log" 2>/dev/null
exit 127
