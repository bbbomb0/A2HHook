#!/system/bin/sh
set -u

# Read-only evidence collector for the HyperOS 3.0.305 runtime jump seen at
# is_A2H_app. This script is development-only and excluded from module ZIPs.

MODDIR=${A2H_MODULE_DIR:-/data/adb/modules/a2h_hook}
FUNC_OFF_HEX=${A2H_FUNC_OFF_HEX:-3e4020}
STAMP=$(date +%Y%m%d_%H%M%S 2>/dev/null || printf unknown)
OUT=${1:-/sdcard/Download/A2H_OS305_runtime_$STAMP.txt}

mkdir -p "$(dirname "$OUT")" 2>/dev/null || true
exec >"$OUT" 2>&1

section() {
  printf '\n===== %s =====\n' "$1"
}

# Android's /system/bin/sh may truncate arithmetic to 32 bits. Keep process
# addresses as hexadecimal strings and add them one nibble at a time.
hex_add() {
  awk -v lhs="$1" -v rhs="$2" 'BEGIN {
    digits = "0123456789abcdef"
    lhs = tolower(lhs); rhs = tolower(rhs)
    width = length(lhs) > length(rhs) ? length(lhs) : length(rhs)
    carry = 0; result = ""
    for (position = 0; position < width; position++) {
      left = position < length(lhs) ? substr(lhs, length(lhs) - position, 1) : ""
      right = position < length(rhs) ? substr(rhs, length(rhs) - position, 1) : ""
      left_value = left == "" ? 0 : index(digits, left) - 1
      right_value = right == "" ? 0 : index(digits, right) - 1
      if (left_value < 0 || right_value < 0) exit 2
      sum = left_value + right_value + carry
      result = substr(digits, (sum % 16) + 1, 1) result
      carry = int(sum / 16)
    }
    if (carry) result = substr(digits, carry + 1, 1) result
    print result
  }'
}

print_target_map() {
  awk -v target="$1" '
    function normalize(value) {
      value = tolower(value)
      sub(/^0+/, "", value)
      return value == "" ? "0" : value
    }
    function compare(left, right, left_length, right_length) {
      left = normalize(left); right = normalize(right)
      left_length = length(left); right_length = length(right)
      if (left_length != right_length) return left_length < right_length ? -1 : 1
      if (("x" left) == ("x" right)) return 0
      return ("x" left) < ("x" right) ? -1 : 1
    }
    {
      split($1, range, "-")
      if (compare(target, range[1]) >= 0 && compare(target, range[2]) < 0) print
    }
  ' "$MAPS"
}

section identity
date
id
for prop in \
  ro.product.model \
  ro.product.device \
  ro.build.version.release \
  ro.build.version.sdk \
  ro.mi.os.version.incremental \
  ro.system.build.version.incremental \
  ro.vendor.build.version.incremental; do
  printf '%s=%s\n' "$prop" "$(getprop "$prop")"
done
printf 'wrap.vendor.audio-hal-aidl=%s\n' "$(getprop wrap.vendor.audio-hal-aidl)"

section module
if [ -f "$MODDIR/module.prop" ]; then
  cat "$MODDIR/module.prop"
else
  printf 'module.prop missing: %s\n' "$MODDIR/module.prop"
fi
for file in \
  "$MODDIR/bin/a2h_patch" \
  "$MODDIR/bin/a2h_inject" \
  "$MODDIR/zygisk/arm64-v8a.so" \
  "$MODDIR/zygisk/arm64-v8a/a2h_hook.so" \
  "$MODDIR/system/lib64/liba2h_hook.so"; do
  ls -l "$file" 2>/dev/null || true
done
sha256sum "$MODDIR/bin/a2h_patch" 2>/dev/null || true

section configuration
for name in state packages.txt package_states config_generation func_off cave_off; do
  file="$MODDIR/config/$name"
  printf '%s:\n' "$file"
  if [ -f "$file" ]; then
    awk '{ printf "%02d: %s\n", NR, $0 }' "$file"
  else
    printf '(missing)\n'
  fi
done

PID=$(pidof android.hardware.audio.service-aidl.mediatek 2>/dev/null | awk '{print $1}')
if [ -z "${PID:-}" ]; then
  PID=$(pidof android.hardware.audio.service 2>/dev/null | awk '{print $1}')
fi

section process
if [ -z "${PID:-}" ]; then
  printf 'audio HAL process not found\n'
  printf '\nOUTPUT=%s\n' "$OUT"
  exit 2
fi
printf 'pid=%s\n' "$PID"
printf 'cmdline='
tr '\000' ' ' < "/proc/$PID/cmdline" 2>/dev/null || true
printf '\n'
printf 'selected environment variables:\n'
tr '\000' '\n' < "/proc/$PID/environ" 2>/dev/null |
  grep -E '^(LD_PRELOAD|LD_LIBRARY_PATH|CLASSPATH)=' || true

MAPS="/proc/$PID/maps"
section audio_maps
grep -E 'audio\.primary\..*\.so' "$MAPS" 2>/dev/null || true

section hook_related_maps
grep -Ei 'a2h|dobby|zygisk|riru|lsposed|substrate|frida' "$MAPS" 2>/dev/null || true

HAL_LINE=$(awk '$3 == "00000000" && $0 ~ /audio\.primary\..*\.so/ { print; exit }' "$MAPS" 2>/dev/null)
BASE_HEX=$(printf '%s\n' "$HAL_LINE" | awk '{ split($1, range, "-"); print range[1] }')
HAL_PATH=$(printf '%s\n' "$HAL_LINE" | awk '{ print $NF }')

section mapped_hal
printf 'line=%s\n' "${HAL_LINE:-missing}"
printf 'path=%s\n' "${HAL_PATH:-missing}"
if [ -n "${HAL_PATH:-}" ] && [ -f "$HAL_PATH" ]; then
  ls -l "$HAL_PATH"
  sha256sum "$HAL_PATH" 2>/dev/null || true
fi

section runtime_function
printf 'base_hex=%s func_off_hex=%s\n' "${BASE_HEX:-missing}" "$FUNC_OFF_HEX"
case "${BASE_HEX:-}" in
  ''|*[!0-9A-Fa-f]*)
    printf 'cannot derive mapped HAL base\n'
    ;;
  *)
    FUNC_HEX=$(hex_add "$BASE_HEX" "$FUNC_OFF_HEX") || FUNC_HEX=
    if [ -z "$FUNC_HEX" ]; then
      printf 'cannot add mapped HAL base and function offset\n'
      printf '\nOUTPUT=%s\n' "$OUT"
      exit 3
    fi
    printf 'func_abs=0x%s\n' "$FUNC_HEX"
    printf 'live_160_bytes:\n'
    dd if="/proc/$PID/mem" bs=1 skip="0x$FUNC_HEX" count=160 2>/dev/null |
      od -An -tx1 -v || true
    HEAD_HEX=$(dd if="/proc/$PID/mem" bs=1 skip="0x$FUNC_HEX" count=8 2>/dev/null |
      od -An -tx1 -v | tr -d ' \r\n')
    printf 'head_8=%s\n' "${HEAD_HEX:-unreadable}"
    if [ "$HEAD_HEX" = 5100005820021fd6 ]; then
      TARGET_AT_HEX=$(hex_add "$FUNC_HEX" 8) || TARGET_AT_HEX=
      printf 'jump_target_bytes_at_plus_8:\n'
      TARGET_HEX=$(dd if="/proc/$PID/mem" bs=1 skip="0x$TARGET_AT_HEX" count=8 2>/dev/null |
        od -An -tx8 -v | tr -d ' \r\n')
      printf '%s\n' "${TARGET_HEX:-unreadable}"
      case "${TARGET_HEX:-}" in
        ''|*[!0-9A-Fa-f]*) ;;
        *)
        printf 'jump_target=0x%s\n' "$TARGET_HEX"
        printf 'jump_target_map:\n'
        print_target_map "$TARGET_HEX"
        printf 'jump_target_128_bytes:\n'
        dd if="/proc/$PID/mem" bs=1 skip="0x$TARGET_HEX" count=128 2>/dev/null |
          od -An -tx1 -v || true
        ;;
      esac
    else
      printf 'absolute_jump=no\n'
    fi
    ;;
esac

section full_maps
cat "$MAPS" 2>/dev/null || true

section recent_logs
for file in "$MODDIR/a2h_patch.log" "$MODDIR/action.log"; do
  printf '%s (last 160 lines):\n' "$file"
  tail -n 160 "$file" 2>/dev/null || printf '(missing)\n'
done

printf '\nOUTPUT=%s\n' "$OUT"
