#!/system/bin/sh
# The device node belongs to gzvm.ko and stays behind; we only drop our own
# module.
rmmod gzvm_unlock 2>/dev/null
rm -f /data/local/tmp/mgz_unlock.log
