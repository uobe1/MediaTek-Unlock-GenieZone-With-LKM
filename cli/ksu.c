// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * mgz install / remove / ksu-keep-alive
 *
 * Persistence follows the KernelSU module layout documented at
 * https://kernelsu.org/guide/module.html :
 *
 *   /data/adb/modules/<id>/{module.prop,service.sh,late-load.sh,...}
 *
 * Two boot stages are covered so both root flavours keep working:
 *   - service.sh    late_start service stage, standard boot (recommended)
 *   - late-load.sh  late-load mode, replaces post-fs-data.sh
 *
 * The module is loaded with `ksud insmod` whenever ksud exists, because that
 * loads the module with kallsyms access, which kprobe resolution needs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "mgz.h"

#define MGZ_STAGE_DIR "/data/local/tmp/.mgz_stage"
#define MGZ_KEEP_ALIVE_LOG "/data/local/tmp/mgz_unlock.log"

static const char *const load_sh =
	"#!/system/bin/sh\n"
	"# Shared loader used by service.sh, late-load.sh, boot-completed.sh\n"
	"# and action.sh.  Keeps one implementation for every boot stage.\n"
	"MODDIR=${0%/*}\n"
	"KO=\"$MODDIR/gzvm_unlock.ko\"\n"
	"LOG=" MGZ_KEEP_ALIVE_LOG "\n"
	"\n"
	"log() { echo \"[mgz] $1\" >> \"$LOG\" 2>&1; }\n"
	"\n"
	"wait_for_gzvm() {\n"
	"  i=0\n"
	"  while [ $i -lt 30 ]; do\n"
	"    if grep -q '^gzvm ' /proc/modules; then return 0; fi\n"
	"    i=$((i + 1))\n"
	"    sleep 1\n"
	"  done\n"
	"  return 1\n"
	"}\n"
	"\n"
	"load_gzvm_if_needed() {\n"
	"  grep -q '^gzvm ' /proc/modules && return 0\n"
	"  for ko in /system_dlkm/lib/modules/gzvm.ko \\\n"
	"            /vendor_dlkm/lib/modules/gzvm.ko \\\n"
	"            /vendor/lib/modules/gzvm.ko \\\n"
	"            /odm/lib/modules/gzvm.ko; do\n"
	"    if [ -f \"$ko\" ]; then\n"
	"      insmod \"$ko\" && return 0\n"
	"    fi\n"
	"  done\n"
	"  return 1\n"
	"}\n"
	"\n"
	"mgz_load() {\n"
	"  if [ -e /dev/gzvm ]; then\n"
	"    log 'already active, /dev/gzvm exists'\n"
	"    return 0\n"
	"  fi\n"
	"\n"
	"  load_gzvm_if_needed || { log 'gzvm.ko is not available'; return 1; }\n"
	"  wait_for_gzvm || { log 'gzvm.ko did not show up'; return 1; }\n"
	"\n"
	"  if [ -x /data/adb/ksu/bin/ksud ]; then\n"
	"    /data/adb/ksu/bin/ksud insmod \"$KO\"\n"
	"    rc=$?\n"
	"  else\n"
	"    insmod \"$KO\"\n"
	"    rc=$?\n"
	"  fi\n"
	"\n"
	"  if [ -e /dev/gzvm ]; then\n"
	"    log 'activated, /dev/gzvm is present'\n"
	"    return 0\n"
	"  fi\n"
	"\n"
	"  log \"activation failed (insmod rc=$rc)\"\n"
	"  return 1\n"
	"}\n";

static const char *const service_sh =
	"#!/system/bin/sh\n"
	"# late_start service stage: non-blocking, recommended by KernelSU.\n"
	"MODDIR=${0%/*}\n"
	". \"$MODDIR/load.sh\"\n"
	"mgz_load\n";

static const char *const late_load_sh =
	"#!/system/bin/sh\n"
	"# late-load mode: runs instead of post-fs-data.sh, before OverlayFS\n"
	"# mounting, for root solutions that load kernelsu.ko after boot.\n"
	"MODDIR=${0%/*}\n"
	". \"$MODDIR/load.sh\"\n"
	"mgz_load\n";

static const char *const boot_completed_sh =
	"#!/system/bin/sh\n"
	"# Last chance: verify after boot completed and retry once.\n"
	"MODDIR=${0%/*}\n"
	". \"$MODDIR/load.sh\"\n"
	"mgz_load\n";

static const char *const action_sh =
	"#!/system/bin/sh\n"
	"# Manual trigger from the KernelSU manager Action button.\n"
	"MODDIR=${0%/*}\n"
	". \"$MODDIR/load.sh\"\n"
	"if mgz_load; then\n"
	"  echo 'GenieZone is active (/dev/gzvm)'\n"
	"else\n"
	"  echo 'Activation failed, see " MGZ_KEEP_ALIVE_LOG "'\n"
	"fi\n";

static const char *const uninstall_sh =
	"#!/system/bin/sh\n"
	"# The device node belongs to gzvm.ko and stays behind; we only drop\n"
	"# our own module.\n"
	"rmmod gzvm_unlock 2>/dev/null\n"
	"rm -f " MGZ_KEEP_ALIVE_LOG "\n";

static int module_dir(char *out, size_t outsz)
{
	snprintf(out, outsz, "%s/%s", MGZ_KSU_MODULES_DIR, MGZ_MODULE_ID);
	return 0;
}

static int stage_module(const char *ko_path)
{
	char staged_ko[512];
	char prop[1024];

	if (mgz_dir_exists(MGZ_STAGE_DIR))
		mgz_rmtree(MGZ_STAGE_DIR);

	if (mgz_mkdirs(MGZ_STAGE_DIR) != 0) {
		mgz_log(MGZ_LOG_ERR, "cannot create %s", MGZ_STAGE_DIR);
		return -1;
	}

	snprintf(prop, sizeof(prop),
		 "id=%s\n"
		 "name=%s\n"
		 "version=v%s\n"
		 "versionCode=1\n"
		 "author=MediaTek-Unlock-GenieZone-With-LKM contributors\n"
		 "description=Load gzvm_unlock.ko on boot to expose /dev/gzvm "
		 "when the device tree hides GenieZone.\n",
		 MGZ_MODULE_ID, MGZ_MODULE_NAME, MGZ_VERSION);

	if (mgz_write_file(MGZ_STAGE_DIR "/module.prop", prop, 0644) != 0 ||
	    mgz_write_file(MGZ_STAGE_DIR "/load.sh", load_sh, 0755) != 0 ||
	    mgz_write_file(MGZ_STAGE_DIR "/service.sh", service_sh, 0755) != 0 ||
	    mgz_write_file(MGZ_STAGE_DIR "/late-load.sh", late_load_sh,
			   0755) != 0 ||
	    mgz_write_file(MGZ_STAGE_DIR "/boot-completed.sh",
			   boot_completed_sh, 0755) != 0 ||
	    mgz_write_file(MGZ_STAGE_DIR "/action.sh", action_sh, 0755) != 0 ||
	    mgz_write_file(MGZ_STAGE_DIR "/uninstall.sh", uninstall_sh,
			   0755) != 0 ||
	    mgz_write_file(MGZ_STAGE_DIR "/skip_mount", "", 0644) != 0) {
		mgz_log(MGZ_LOG_ERR, "cannot write the module files");
		return -1;
	}

	snprintf(staged_ko, sizeof(staged_ko), "%s/%s", MGZ_STAGE_DIR,
		 MGZ_UNLOCK_KO);
	if (mgz_copy_file(ko_path, staged_ko) != 0) {
		mgz_log(MGZ_LOG_ERR, "cannot copy %s", ko_path);
		return -1;
	}

	return 0;
}

int cmd_ksu_keep_alive(int argc, char **argv, const struct mgz_root *root)
{
	char ko_path[512];
	char dir[512];
	char cmd[1024];
	char out[2048];
	const char *hint = (argc > 0) ? argv[0] : NULL;

	if (!root->ksud) {
		mgz_log(MGZ_LOG_ERR,
			"ksud not found; persistence requires KernelSU");
		return -1;
	}

	if (!mgz_find_module_file(hint, MGZ_UNLOCK_KO, ko_path,
				  sizeof(ko_path))) {
		mgz_log(MGZ_LOG_ERR, "%s not found (pass its path as argument)",
			 MGZ_UNLOCK_KO);
		return -1;
	}

	mgz_log(MGZ_LOG_INFO, "staging KernelSU module from %s", ko_path);
	if (stage_module(ko_path) != 0)
		return -1;

	module_dir(dir, sizeof(dir));

	snprintf(cmd, sizeof(cmd),
		 "rm -rf %s && mkdir -p %s && cp -f %s/* %s/ && "
		 "chmod 0755 %s && chmod 0755 %s/*.sh && "
		 "chmod 0644 %s/module.prop %s/%s %s/skip_mount",
		 dir, dir, MGZ_STAGE_DIR, dir, dir, dir, dir, dir,
		 MGZ_UNLOCK_KO, dir);

	if (mgz_root_exec(root, cmd, out, sizeof(out)) != 0) {
		mgz_log(MGZ_LOG_ERR, "cannot install into %s: %s", dir, out);
		return -1;
	}

	mgz_rmtree(MGZ_STAGE_DIR);

	mgz_log(MGZ_LOG_OK, "persisted at %s", dir);
	mgz_log(MGZ_LOG_INFO,
		"service.sh covers standard boot, late-load.sh covers "
		"late-load root solutions");

	if (mgz_root_exec_ksud(root, "module list", out, sizeof(out)) == 0)
		mgz_log(MGZ_LOG_DEBUG, "ksud module list: %s", out);

	mgz_log(MGZ_LOG_INFO, "reboot to activate, or run the module action");

	return 0;
}

int cmd_install(int argc, char **argv, const struct mgz_root *root)
{
	char ko_path[512];
	char quoted[512];
	char cmd[1024];
	char out[4096];
	const char *hint = (argc > 0) ? argv[0] : NULL;
	int rc;

	if (!mgz_find_module_file(hint, MGZ_UNLOCK_KO, ko_path,
				  sizeof(ko_path))) {
		mgz_log(MGZ_LOG_ERR, "%s not found (pass its path as argument)",
			 MGZ_UNLOCK_KO);
		return -1;
	}

	mgz_log(MGZ_LOG_INFO, "using %s", ko_path);

	/* gzvm.ko must be resident before its probe entry point is called. */
	snprintf(cmd, sizeof(cmd),
		 "grep -q '^gzvm ' /proc/modules || "
		 "for ko in /system_dlkm/lib/modules/gzvm.ko "
		 "/vendor_dlkm/lib/modules/gzvm.ko "
		 "/vendor/lib/modules/gzvm.ko /odm/lib/modules/gzvm.ko; do "
		 "[ -f \"$ko\" ] && insmod \"$ko\" && break; done; "
		 "grep -q '^gzvm ' /proc/modules");
	if (mgz_root_exec(root, cmd, out, sizeof(out)) != 0) {
		mgz_log(MGZ_LOG_ERR, "gzvm.ko is not loaded and could not be "
				     "loaded: %s", out);
		return -1;
	}

	mgz_shell_quote(ko_path, quoted, sizeof(quoted));

	/*
	 * ksud insmod loads the module with kallsyms access, which is what
	 * kprobe based symbol resolution needs; plain insmod is the fallback.
	 */
	snprintf(cmd, sizeof(cmd), "insmod %s", quoted);
	if (root->ksud)
		rc = mgz_root_exec_ksud(root, cmd, out, sizeof(out));
	else
		rc = mgz_root_exec(root, cmd, out, sizeof(out));

	if (rc != 0) {
		mgz_log(MGZ_LOG_ERR, "insmod failed: %s", out);
		snprintf(cmd, sizeof(cmd), "dmesg | tail -20");
		if (mgz_root_exec(root, cmd, out, sizeof(out)) == 0)
			mgz_log(MGZ_LOG_RAW, "%s", out);
		return -1;
	}

	/* /dev/gzvm is a character device, so a plain stat() is enough. */
	if (access(MGZ_DEFAULT_NODE, F_OK) != 0) {
		mgz_log(MGZ_LOG_ERR, "%s was not created", MGZ_DEFAULT_NODE);
		return -1;
	}

	mgz_log(MGZ_LOG_OK, "%s is present", MGZ_DEFAULT_NODE);
	mgz_log(MGZ_LOG_INFO,
		"this survives until reboot; use `mgz ksu-keep-alive` to make "
		"it permanent");

	return 0;
}

int cmd_remove(int argc, char **argv, const struct mgz_root *root)
{
	char dir[512];
	char cmd[1024];
	char out[2048];

	(void)argc;
	(void)argv;

	snprintf(cmd, sizeof(cmd), "rmmod gzvm_unlock");
	if (mgz_root_exec(root, cmd, out, sizeof(out)) != 0)
		mgz_log(MGZ_LOG_DEBUG, "rmmod gzvm_unlock: %s", out);
	else
		mgz_log(MGZ_LOG_OK, "gzvm_unlock unloaded");

	module_dir(dir, sizeof(dir));

	if (root->ksud) {
		snprintf(cmd, sizeof(cmd), "module uninstall %s", MGZ_MODULE_ID);
		if (mgz_root_exec_ksud(root, cmd, out, sizeof(out)) != 0)
			mgz_log(MGZ_LOG_DEBUG, "ksud module uninstall: %s", out);
		else
			mgz_log(MGZ_LOG_OK, "kernelSU module uninstalled");
	}

	if (mgz_root_exec(root, "test -d " MGZ_KSU_MODULES_DIR "/" MGZ_MODULE_ID,
			  NULL, 0) == 0) {
		snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
		if (mgz_root_exec(root, cmd, out, sizeof(out)) == 0)
			mgz_log(MGZ_LOG_OK, "removed %s", dir);
		else
			mgz_log(MGZ_LOG_ERR, "cannot remove %s: %s", dir, out);
	}

	mgz_log(MGZ_LOG_INFO,
		"%s may still exist because it belongs to gzvm.ko; reboot to "
		"clear it", MGZ_DEFAULT_NODE);

	return 0;
}
