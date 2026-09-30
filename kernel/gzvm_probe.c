// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * MediaTek-Unlock-GenieZone-With-LKM
 *
 * gzvm_probe.ko — detection module.
 *
 * Answers two questions that can only be answered from kernel context:
 *
 *   1. Is a hypervisor alive at EL2?  Determined by issuing the GenieZone
 *      probe HVC ourselves; `a0 == 0` means EL2 answered.
 *   2. Is the gzvm driver probe entry point reachable?  Determined by
 *      resolving its address through kallsyms with a kprobe.
 *
 * Every result is published as a read-only module parameter so userspace can
 * read it from /sys/module/gzvm_probe/parameters/ without parsing dmesg.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>

#include "gzvm_common.h"

static const char *const gzvm_probe_candidates[] = GZVM_PROBE_CANDIDATE_LIST;

static unsigned int hvc_fn = GZVM_HVC_PROBE_FN_DEFAULT;
module_param(hvc_fn, uint, 0644);
MODULE_PARM_DESC(hvc_fn, "GenieZone HVC function number used for the EL2 probe");

static bool skip_hvc;
module_param(skip_hvc, bool, 0644);
MODULE_PARM_DESC(skip_hvc, "skip the HVC trap and only resolve symbols");

/* ---- results (read-only) ------------------------------------------------ */

static int el2_alive = -1;
module_param(el2_alive, int, 0444);
MODULE_PARM_DESC(el2_alive, "1 if EL2 answered the GenieZone HVC, 0 if not, -1 if not probed");

static unsigned long hvc_a0;
static unsigned long hvc_a1;
static unsigned long hvc_a2;
static unsigned long hvc_a3;
module_param(hvc_a0, ulong, 0444);
module_param(hvc_a1, ulong, 0444);
module_param(hvc_a2, ulong, 0444);
module_param(hvc_a3, ulong, 0444);
MODULE_PARM_DESC(hvc_a0, "SMCCC result register a0 of the GenieZone probe HVC");
MODULE_PARM_DESC(hvc_a1, "SMCCC result register a1 of the GenieZone probe HVC");
MODULE_PARM_DESC(hvc_a2, "SMCCC result register a2 of the GenieZone probe HVC");
MODULE_PARM_DESC(hvc_a3, "SMCCC result register a3 of the GenieZone probe HVC");

static int symbol_found;
module_param(symbol_found, int, 0444);
MODULE_PARM_DESC(symbol_found, "1 if the gzvm driver probe entry point was resolved");

static unsigned long symbol_addr;
module_param(symbol_addr, ulong, 0444);
MODULE_PARM_DESC(symbol_addr, "runtime address of the resolved probe entry point");

static char symbol_name[64];
module_param_string(symbol_name, symbol_name, sizeof(symbol_name), 0444);
MODULE_PARM_DESC(symbol_name, "name of the resolved probe entry point");

static int verdict = GZVM_VERDICT_UNSUPPORTED;
module_param(verdict, int, 0444);
MODULE_PARM_DESC(verdict, "0 = supported, 1 = likely, 2 = unsupported");

static void gzvm_probe_hvc(void)
{
	struct arm_smccc_res res;

	arm_smccc_hvc(GZVM_HCALL_ID(hvc_fn), 0, 0, 0, 0, 0, 0, 0, &res);

	hvc_a0 = res.a0;
	hvc_a1 = res.a1;
	hvc_a2 = res.a2;
	hvc_a3 = res.a3;
	el2_alive = (res.a0 == 0);

	pr_info("HVC(%u) a0=0x%lx a1=0x%lx a2=0x%lx a3=0x%lx\n",
		hvc_fn, hvc_a0, hvc_a1, hvc_a2, hvc_a3);
	pr_info("EL2 hypervisor: %s\n", el2_alive ? "ALIVE" : "NO RESPONSE");
}

static void gzvm_probe_symbols(void)
{
	void *addr = NULL;
	unsigned int i;

	for (i = 0; gzvm_probe_candidates[i]; i++) {
		if (gzvm_resolve_symbol(gzvm_probe_candidates[i], &addr) == 0) {
			symbol_found = 1;
			symbol_addr = (unsigned long)addr;
			strscpy(symbol_name, gzvm_probe_candidates[i],
				sizeof(symbol_name));
			pr_info("resolved %s @ %ps\n", symbol_name, addr);
			return;
		}
	}

	pr_warn("no gzvm probe symbol in kallsyms; is gzvm.ko loaded?\n");
}

static int __init gzvm_probe_init(void)
{
	gzvm_probe_symbols();

	if (!skip_hvc)
		gzvm_probe_hvc();

	if (el2_alive == 1 && symbol_found)
		verdict = GZVM_VERDICT_SUPPORTED;
	else if (symbol_found || el2_alive == -1)
		verdict = GZVM_VERDICT_LIKELY;
	else
		verdict = GZVM_VERDICT_UNSUPPORTED;

	pr_info("verdict=%d (%s)\n", verdict, gzvm_verdict_name(verdict));

	return 0;
}

static void __exit gzvm_probe_exit(void)
{
	pr_info("exit\n");
}

module_init(gzvm_probe_init);
module_exit(gzvm_probe_exit);

/*
 * "GPL" is the legacy tag modpost accepts as GPL compatible; the less common
 * spellings such as "GPL v3" are treated as proprietary and would make every
 * GPL-only symbol (register_kprobe among them) unavailable. The sources are
 * GPLv3-or-later, see the SPDX header and the repository LICENSE.
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("MediaTek-Unlock-GenieZone-With-LKM contributors");
MODULE_DESCRIPTION("Detect whether MediaTek GenieZone is alive at EL2");
MODULE_VERSION("0.1.0");
