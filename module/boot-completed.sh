#!/system/bin/sh
# Last chance: verify after boot completed and retry once.
MODDIR=${0%/*}
. "$MODDIR/load.sh"
. "$MODDIR/status.sh"
mgz_load
update_module_description
