#!/system/bin/sh
# Last chance: verify after boot completed and retry once.
MODDIR=${0%/*}
. "$MODDIR/load.sh"
mgz_load
