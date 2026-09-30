#!/system/bin/sh
# late_start service stage: non-blocking, runs in parallel with the rest of
# boot. This is the stage KernelSU recommends for most scripts.
MODDIR=${0%/*}
. "$MODDIR/load.sh"
mgz_load
