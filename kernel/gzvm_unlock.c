// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * MediaTek-Unlock-GenieZone-With-LKM
 *
 * gzvm_unlock.ko — activation module.
 *
 * On devices whose device tree lacks a "mediatek,geniezone*" node the gzvm
 * platform driver is never bound, so its probe() callback never runs and
 * /dev/gzvm is never created.  gzvm_drv_probe() does not dereference its
 * platform_device argument, so calling it with NULL performs the complete
 * probe sequence anyway.
 *
 * This module resolves that function through kallsyms (kprobe), calls it with
 * NULL and verifies that the device node appeared.  It writes nothing to the
 * device tree and patches nothing in the kernel.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/platform_device.h>

#include "gzvm_common.h"

static const char *const gzvm_probe_candidates[] = GZVM_PROBE_CANDIDATE_LIST;

static char *symbol;
module_param(symbol, charp, 0644);
MODULE_PARM_DESC(symbol, "override the probe entry point symbol name");

static char *devnode = GZVM_DEVICE_NODE_DEFAULT;
module_param(devnode, charp, 0644);
MODULE_PARM_DESC(devnode, "device node gzvm_drv_probe() is expected to create");

static unsigned int retries = 3;
module_param(retries, uint, 0644);
MODULE_PARM_DESC(retries, "how many times to call the probe entry point");

static bool force;
module_param(force, bool, 0644);
MODULE_PARM_DESC(force, "probe again even if the device node already exists");

/* ---- results (read-only) ------------------------------------------------ */

static int probe_ret = -1;
module_param(probe_ret, int, 0444);
MODULE_PARM_DESC(probe_ret, "return value of the last probe entry point call");

static int node_created;
module_param(node_created, int, 0444);
MODULE_PARM_DESC(node_created, "1 if the device node exists after probing");

static unsigned long probe_addr;
module_param(probe_addr, ulong, 0444);
MODULE_PARM_DESC(probe_addr, "runtime address of the resolved probe entry point");

static bool gzvm_node_exists(const char *path)
{
	struct path p;

	if (kern_path(path, LOOKUP_FOLLOW, &p))
		return false;

	path_put(&p);
	return true;
}

/*
 * device_create() hands the node over to devtmpfs, which creates it from its
 * own thread, so the node can appear a moment after misc_register() returned.
 * Give it a short grace period before deciding the probe failed.
 */
static bool gzvm_wait_for_node(const char *path)
{
	unsigned int i;

	for (i = 0; i < 20; i++) {
		if (gzvm_node_exists(path))
			return true;
		msleep(100);
	}

	return false;
}

static void *gzvm_resolve_probe_entry(void)
{
	void *addr = NULL;
	unsigned int i;

	if (symbol && symbol[0]) {
		if (gzvm_resolve_symbol(symbol, &addr) == 0)
			return addr;
		pr_err("symbol '%s' not found in kallsyms\n", symbol);
		return NULL;
	}

	for (i = 0; gzvm_probe_candidates[i]; i++) {
		if (gzvm_resolve_symbol(gzvm_probe_candidates[i], &addr) == 0) {
			pr_info("resolved %s @ %ps\n",
				gzvm_probe_candidates[i], addr);
			return addr;
		}
	}

	return NULL;
}

static int __init gzvm_unlock_init(void)
{
	int (*probe_entry)(struct platform_device *pdev);
	void *addr;
	unsigned int attempt;

	addr = gzvm_resolve_probe_entry();
	if (!addr) {
		pr_err("no gzvm probe symbol found; load gzvm.ko first\n");
		return -ENODEV;
	}

	probe_addr = (unsigned long)addr;

	if (!force && gzvm_node_exists(devnode)) {
		pr_info("%s already exists, nothing to do\n", devnode);
		node_created = 1;
		return 0;
	}

	probe_entry = (int (*)(struct platform_device *))addr;

	for (attempt = 1; attempt <= retries; attempt++) {
		probe_ret = probe_entry(NULL);
		pr_info("probe(NULL) returned %d (attempt %u/%u)\n",
			probe_ret, attempt, retries);

		if (probe_ret == 0)
			break;
		/* -EBUSY from misc_register() means the node is already there. */
		if (probe_ret == -EBUSY && gzvm_node_exists(devnode))
			break;

		if (attempt < retries)
			msleep(200);
	}

	node_created = gzvm_wait_for_node(devnode);
	if (!node_created) {
		pr_err("%s was not created; GenieZone is most likely disabled "
		       "below Linux (ATF/EL2)\n", devnode);
		return probe_ret ? probe_ret : -ENODEV;
	}

	pr_info("%s is now present\n", devnode);

	return 0;
}

static void __exit gzvm_unlock_exit(void)
{
	/*
	 * The device node belongs to gzvm.ko, so it intentionally stays
	 * behind; unloading this module only removes the activation.
	 */
	pr_info("exit, %s left in place\n", devnode);
}

module_init(gzvm_unlock_init);
module_exit(gzvm_unlock_exit);

/*
 * "GPL" is the legacy tag modpost accepts as GPL compatible; "GPL v3" is not
 * recognised and would turn every GPL-only symbol into an error. Sources are
 * GPLv3-or-later, see the SPDX header and the repository LICENSE.
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("MediaTek-Unlock-GenieZone-With-LKM contributors");
MODULE_DESCRIPTION("Force the MediaTek GenieZone gzvm driver to probe");
MODULE_VERSION("0.1.0");
