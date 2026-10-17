#!/system/bin/sh
# Shared status collection for the boot stages, the action menu and the
# webui panel. Updating module.prop is the Zygisk Next / Droidspaces trick:
# the manager renders the description on the module card, so writing the
# running state into that line gives every manager a live status summary
# without any webui.
MODDIR=${0%/*}
LOG_DIR="$MODDIR/logs"
LOG="$LOG_DIR/mgz_unlock.log"

# One line: "✅ /dev/gzvm active | ✅ gzvm.ko | ❌ gzvm_unlock.ko"
status_summary() {
  if [ -e /dev/gzvm ]; then
    s="✅ /dev/gzvm active"
  else
    s="❌ /dev/gzvm missing"
  fi
  if grep -q '^gzvm ' /proc/modules 2>/dev/null; then
    s="$s | ✅ gzvm.ko"
  else
    s="$s | ❌ gzvm.ko"
  fi
  if grep -q '^gzvm_unlock ' /proc/modules 2>/dev/null; then
    s="$s | ✅ gzvm_unlock.ko"
  else
    s="$s | ❌ gzvm_unlock.ko"
  fi
  echo "$s"
}

# Rewrite the description line of module.prop in place. '#' delimits the sed
# expression because the summary itself contains ' | ' separators.
update_module_description() {
  prop="$MODDIR/module.prop"
  [ -f "$prop" ] || return 0
  desc="$(status_summary) | $(date '+%m-%d %H:%M')"
  sed "s#^description=.*#description=$desc#" "$prop" > "$prop.tmp" 2>/dev/null &&
    mv -f "$prop.tmp" "$prop"
}
