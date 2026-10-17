#!/system/bin/sh
# late_start service stage: non-blocking, recommended by KernelSU.
MODDIR=${0%/*}
. "$MODDIR/load.sh"
. "$MODDIR/status.sh"
mgz_load
update_module_description
