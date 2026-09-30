#!/system/bin/sh
# Shared loader used by service.sh, late-load.sh, boot-completed.sh and
# action.sh, so every boot stage runs exactly the same logic.
#
# Warning: this file is mirrored verbatim in cli/ksu.c (const load_sh).
# Keep both copies identical.
MODDIR=${0%/*}
KO="$MODDIR/gzvm_unlock.ko"
LOG=/data/local/tmp/mgz_unlock.log

log() { echo "[mgz] $1" >> "$LOG" 2>&1; }

wait_for_gzvm() {
  i=0
  while [ $i -lt 30 ]; do
    if grep -q '^gzvm ' /proc/modules; then return 0; fi
    i=$((i + 1))
    sleep 1
  done
  return 1
}

load_gzvm_if_needed() {
  grep -q '^gzvm ' /proc/modules && return 0
  for ko in /system_dlkm/lib/modules/gzvm.ko \
            /vendor_dlkm/lib/modules/gzvm.ko \
            /vendor/lib/modules/gzvm.ko \
            /odm/lib/modules/gzvm.ko; do
    if [ -f "$ko" ]; then
      insmod "$ko" && return 0
    fi
  done
  return 1
}

mgz_load() {
  if [ -e /dev/gzvm ]; then
    log 'already active, /dev/gzvm exists'
    return 0
  fi

  load_gzvm_if_needed || { log 'gzvm.ko is not available'; return 1; }
  wait_for_gzvm || { log 'gzvm.ko did not show up'; return 1; }

  # ksud insmod loads the module with kallsyms access, which is what kprobe
  # based symbol resolution needs.
  if [ -x /data/adb/ksu/bin/ksud ]; then
    /data/adb/ksu/bin/ksud insmod "$KO"
    rc=$?
  else
    insmod "$KO"
    rc=$?
  fi

  if [ -e /dev/gzvm ]; then
    log 'activated, /dev/gzvm is present'
    return 0
  fi

  log "activation failed (insmod rc=$rc)"
  return 1
}
