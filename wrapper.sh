#!/system/bin/sh
MODDIR=/data/adb/modules/a2h_hook
PRELOAD="$MODDIR/zygisk/arm64-v8a/a2h_hook.so"
REAL="/vendor/bin/hw/android.hardware.audio.service-aidl.mediatek"

if [ -f "$PRELOAD" ]; then
  export LD_PRELOAD="$PRELOAD${LD_PRELOAD:+:$LD_PRELOAD}"
fi
exec "$REAL" "$@"
