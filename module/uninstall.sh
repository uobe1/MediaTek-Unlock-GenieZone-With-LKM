#!/system/bin/sh
# The device node belongs to gzvm.ko and stays behind; we only drop our own
# module and our own logs.
MODDIR=${0%/*}
rmmod gzvm_unlock 2>/dev/null
rm -f "$MODDIR"/logs/mgz_unlock.log*
