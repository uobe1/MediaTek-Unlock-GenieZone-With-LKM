#!/system/bin/sh
# late-load mode: runs instead of post-fs-data.sh, before OverlayFS mounting,
# for root solutions that load kernelsu.ko only after boot.
MODDIR=${0%/*}
. "$MODDIR/load.sh"
mgz_load
