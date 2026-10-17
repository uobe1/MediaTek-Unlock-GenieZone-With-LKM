#!/system/bin/sh
# Manual trigger from the KernelSU manager Action button.
#
# Volume key menu (the manager shows stdout in the Action tab):
#   Vol-up   (or 10 s timeout) : exit, do nothing — the default
#   Vol-down                   : install the module
#
# The volume key handling follows the getevent pattern used across Magisk /
# KernelSU module installers: stream /dev/input events, filter for the
# volume keys, and fall back to the safe default on timeout.
MODDIR=${0%/*}
. "$MODDIR/load.sh"
. "$MODDIR/status.sh"

status() {
  if [ -e /dev/gzvm ]; then
    echo '[ok] /dev/gzvm exists'
  else
    echo '[..] /dev/gzvm is missing'
  fi
  if grep -q '^gzvm ' /proc/modules 2>/dev/null; then
    echo '[ok] gzvm.ko is loaded'
  else
    echo '[..] gzvm.ko is not loaded'
  fi
  if grep -q '^gzvm_unlock ' /proc/modules 2>/dev/null; then
    echo '[ok] gzvm_unlock.ko is loaded'
  else
    echo '[..] gzvm_unlock.ko is not loaded'
  fi
}

# Block until a volume key is pressed or the timeout expires. Returns 0 for
# vol-up / timeout (the safe default), 1 for vol-down. getevent keeps
# streaming events; grep -m1 closes the pipe on the first volume key and the
# writer dies of SIGPIPE. Other keys (touch, power) are just filtered out.
volume_choice() {
  line=$(timeout "$1" getevent -ql 2>/dev/null | grep -m1 'KEY_VOLUME')
  case "$line" in
    *KEY_VOLUMEDOWN*) return 1 ;;
    *) return 0 ;;
  esac
}

install_module() {
  if [ -x "$MODDIR/mgz" ]; then
    # Full CLI install: gzvm.ko, gzvm_unlock.ko, vermagic checks, /dev/gzvm.
    "$MODDIR/mgz" install
  else
    # No CLI in the zip: fall back to the shared loader.
    if mgz_load; then
      echo '[ok] GenieZone is active (/dev/gzvm)'
    else
      echo '[fail] activation failed'
      return 1
    fi
  fi
}

echo '== MediaTek GenieZone Unlock =='
echo
status
echo
echo 'Volume keys:'
echo '  Vol-up   : exit, do nothing (default)'
echo '  Vol-down : install the module'
echo
echo "Waiting for a volume key (${1:-10} s)..."
echo

# The first argument overrides the menu timeout; handy for headless testing.
if volume_choice "${1:-10}"; then
  echo 'Vol-up (or timeout): exiting, nothing was done.'
  update_module_description
  exit 0
fi

echo 'Vol-down: installing the module.'
echo

if install_module; then
  echo
  status
  echo
  if [ -x "$MODDIR/mgz" ]; then
    # Prove the driver actually works, not just that the node exists.
    echo 'Running the driver self-test (mgz verity)...'
    echo
    "$MODDIR/mgz" verity
    verity_rc=$?
  else
    verity_rc=0
  fi
  update_module_description
  exit $verity_rc
else
  echo
  echo 'Installation FAILED.'
  echo
  echo "--- log tail ($LOG) ---"
  tail -n 25 "$LOG" 2>/dev/null
  echo
  echo 'The full log, including dmesg diagnostics, is in:'
  echo "  $LOG"
  update_module_description
  exit 1
fi
