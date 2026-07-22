#!/system/bin/sh
MODDIR=/data/adb/modules/a2h_hook
PRELOAD="$MODDIR/zygisk/arm64-v8a/a2h_hook.so"
REAL="/vendor/bin/hw/android.hardware.audio.service-aidl.mediatek"

if [ -f "$PRELOAD" ] && [ -x "$REAL" ]; then
  export LD_PRELOAD="$PRELOAD${LD_PRELOAD:+:$LD_PRELOAD}"
fi

if [ -x "$REAL" ]; then
  exec "$REAL" "$@"
fi

echo "[a2h_hook] audio HAL executable missing: $REAL" >> "$MODDIR/a2h_patch.log" 2>/dev/null
exit 127
