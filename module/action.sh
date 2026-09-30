#!/system/bin/sh
# Manual trigger from the KernelSU manager Action button.
MODDIR=${0%/*}
. "$MODDIR/load.sh"
if mgz_load; then
  echo 'GenieZone is active (/dev/gzvm)'
else
  echo 'Activation failed, see /data/local/tmp/mgz_unlock.log'
fi
