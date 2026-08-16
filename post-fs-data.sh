#!/system/bin/sh
# Remove only this module's retired audio-service wrapper property. The native
# patcher in service.sh is the single runtime path in current releases.

POSTFS_LOCAL_TMP=${A2H_POSTFS_LOCAL_TMP:-/data/local/tmp}
POSTFS_AUDIO_RUNTIME=${A2H_POSTFS_AUDIO_RUNTIME:-$POSTFS_LOCAL_TMP/a2h_hook_runtime}
POSTFS_SERVICE_LOCK=${A2H_POSTFS_SERVICE_LOCK:-$POSTFS_LOCAL_TMP/a2h_hook_service.lock}

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

process_starttime() {
  process_pid=$1
  case "$process_pid" in ''|*[!0-9]*) return 1 ;; esac
  process_stat=
  IFS= read -r process_stat 2>/dev/null < "/proc/$process_pid/stat" || return 1
  process_tail=${process_stat##*) }
  [ "$process_tail" != "$process_stat" ] || return 1
  set -- $process_tail
  [ "$#" -ge 20 ] || return 1
  shift 19
  process_value=$1
  case "$process_value" in ''|*[!0-9]*) return 1 ;; esac
  printf '%s\n' "$process_value"
}

owned_record_alive() {
  owned_record=$1
  owned_process_name=$2
  owned_value=
  [ ! -r "$owned_record" ] || IFS= read -r owned_value < "$owned_record" || true
  owned_pid=${owned_value%%.*}
  owned_start=${owned_value#*.}
  case "$owned_pid" in ''|*[!0-9]*) return 1 ;; esac
  case "$owned_start" in ''|*[!0-9]*) return 1 ;; esac
  [ "$owned_start" != "$owned_value" ] || return 1
  owned_live_start=$(process_starttime "$owned_pid") || return 1
  [ "$owned_live_start" = "$owned_start" ] || return 1
  kill -0 "$owned_pid" 2>/dev/null || return 1
  grep -a -F -q "$owned_process_name" "/proc/$owned_pid/cmdline" 2>/dev/null
}

runtime_owner_alive() {
  owned_record_alive "$POSTFS_SERVICE_LOCK/owner" service.sh && return 0
  owned_record_alive "$POSTFS_AUDIO_RUNTIME/watcher.lock/owner" a2h_audio_watch && return 0
  return 1
}

lock_epoch_signature() {
  for lock_dir in \
    "$POSTFS_SERVICE_LOCK" \
    "$POSTFS_AUDIO_RUNTIME/watcher.lock"; do
    if [ -L "$lock_dir" ]; then
      printf '%s|symlink\n' "$lock_dir"
    elif [ -d "$lock_dir" ]; then
      lock_identity=$(stat -c '%d:%i:%u:%g' "$lock_dir" 2>/dev/null) || return 1
      lock_owner=
      [ ! -r "$lock_dir/owner" ] || IFS= read -r lock_owner < "$lock_dir/owner" || true
      printf '%s|dir|%s|%s\n' "$lock_dir" "$lock_identity" "$lock_owner"
    else
      printf '%s|missing\n' "$lock_dir"
    fi
  done
}

cleanup_previous_boot_runtime() {
  runtime_root=$POSTFS_AUDIO_RUNTIME
  service_lock=$POSTFS_SERVICE_LOCK
  if [ ! -e "$service_lock" ] && [ ! -e "$runtime_root/watcher.lock" ]; then
    cleanup_stale_runtime "$POSTFS_LOCAL_TMP"
    rm -rf "$runtime_root" 2>/dev/null || true
    return 0
  fi

  # Temporary-root activation can run post-fs-data again in the same boot.
  # Preserve a live service/watcher instead of deleting its ownership record.
  runtime_owner_alive && return 75
  lock_epoch_1=$(lock_epoch_signature) || return 75
  sleep 1
  runtime_owner_alive && return 75
  lock_epoch_2=$(lock_epoch_signature) || return 75
  [ "$lock_epoch_1" = "$lock_epoch_2" ] || return 75
  sleep 1
  runtime_owner_alive && return 75
  lock_epoch_3=$(lock_epoch_signature) || return 75
  [ "$lock_epoch_2" = "$lock_epoch_3" ] || return 75

  cleanup_stale_runtime "$POSTFS_LOCAL_TMP"
  rm -rf "$runtime_root" "$service_lock" 2>/dev/null || true
  return 0
}

# /data/local/tmp survives a normal reboot. Clean only a stable, non-live
# previous-boot epoch; sys.boot_completed alone is not an ownership proof.
if [ "$(getprop sys.boot_completed 2>/dev/null)" != "1" ]; then
  cleanup_previous_boot_runtime || true
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
