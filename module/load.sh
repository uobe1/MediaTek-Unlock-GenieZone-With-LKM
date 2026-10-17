#!/system/bin/sh
# Shared loader used by service.sh, late-load.sh, boot-completed.sh and
# action.sh, so every boot stage runs exactly the same logic.
#
# This file is embedded into the mgz binary at build time (cmake turns it
# into <build>/cli/embedded/load_sh.h). Edit it here; the C copy is generated.

MODDIR=${0%/*}
KO="$MODDIR/gzvm_unlock.ko"
LOG_DIR="$MODDIR/logs"
LOG="$LOG_DIR/mgz_unlock.log"
LOCK=/dev/.mgz_load.lock

mkdir -p "$LOG_DIR" 2>/dev/null

log() {
  # Rotate at 64 KiB so a failing boot loop cannot grow the log forever.
  size=0
  [ -f "$LOG" ] && size=$(wc -c < "$LOG" 2>/dev/null)
  [ "$((size + 0))" -gt 65536 ] && mv -f "$LOG" "$LOG.old"
  echo "[mgz $(date '+%Y-%m-%d %H:%M:%S')] $*" >> "$LOG" 2>&1
}

# Append context that helps debugging a failed activation. Read-only: the
# SELinux mode is recorded, never changed.
dump_diagnostics() {
  log '--- diagnostics ---'
  log "SELinux: $(getenforce 2>/dev/null || echo unknown)"
  for p in probe_ret probe_addr node_created; do
    v=$(cat "/sys/module/gzvm_unlock/parameters/$p" 2>/dev/null)
    [ -n "$v" ] && log "gzvm_unlock.$p=$v"
  done
  dmesg 2>/dev/null | tail -n 40 >> "$LOG" 2>&1
  log '--- end of diagnostics ---'
}

load_gzvm_if_needed() {
  grep -q '^gzvm ' /proc/modules && return 0
  # modprobe first: it resolves dependencies from the dlkm modules.dep.
  if command -v modprobe >/dev/null 2>&1 && modprobe gzvm 2>/dev/null; then
    return 0
  fi
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

wait_for_devnode() {
  i=0
  while [ $i -lt 10 ]; do
    if [ -e /dev/gzvm ]; then return 0; fi
    i=$((i + 1))
    sleep 1
  done
  return 1
}

# Another boot stage may still be inside mgz_load; wait for it instead of
# racing it. The lock lives on tmpfs, so a crashed run cannot survive a boot.
wait_for_other_loader() {
  i=0
  while [ -d "$LOCK" ] && [ $i -lt 40 ]; do
    if [ -e /dev/gzvm ]; then return 0; fi
    i=$((i + 1))
    sleep 1
  done
  [ -e /dev/gzvm ]
}

mgz_load() {
  if [ -e /dev/gzvm ]; then
    log 'already active, /dev/gzvm exists'
    return 0
  fi

  # Only wait when the lock directory really exists; a plain mkdir failure
  # (for example a read-only /dev) must not be mistaken for contention.
  if ! mkdir "$LOCK" 2>/dev/null && [ -d "$LOCK" ]; then
    log 'another loader is running, waiting for it'
    if wait_for_other_loader; then
      log 'activated by the other loader, /dev/gzvm is present'
      return 0
    fi
    log 'the other loader did not activate /dev/gzvm'
    return 1
  fi

  # A failure here is not fatal: when gzvm is built-in there is no .ko on
  # disk yet the probe symbol is already in the kernel, and when gzvm.ko was
  # loaded concurrently the grep above may simply have lost the race. Either
  # way the insmod below is the real verdict.
  load_gzvm_if_needed || \
    log 'gzvm.ko is not available as a module; it may be built-in'

  # Plain insmod already works with MODVERSIONS (the release part of the
  # vermagic is ignored); ksud insmod is the fallback when it is blocked.
  # ksud loads through the kernel itself, so no SELinux handling is needed.
  insmod "$KO"
  rc=$?
  if [ $rc -ne 0 ] && [ -x /data/adb/ksu/bin/ksud ]; then
    /data/adb/ksu/bin/ksud insmod "$KO"
    rc=$?
  fi

  if [ $rc -eq 0 ]; then
    # devtmpfs creates the node from its own thread; give it a moment.
    if wait_for_devnode; then
      log 'activated, /dev/gzvm is present'
    else
      log 'insmod succeeded but /dev/gzvm never showed up'
      dump_diagnostics
      rc=1
    fi
  else
    log "activation failed (insmod rc=$rc)"
    dump_diagnostics
  fi

  rmdir "$LOCK" 2>/dev/null
  return $rc
}
