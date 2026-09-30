/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * MediaTek-Unlock-GenieZone-With-LKM
 *
 * Shared definitions for gzvm_probe.ko and gzvm_unlock.ko.
 */
#ifndef _GZVM_COMMON_H
#define _GZVM_COMMON_H

#include <linux/arm-smccc.h>
#include <linux/kprobes.h>

/*
 * GenieZone hypervisor calls live in the vendor-hyp SMCCC range.  Function 0
 * is MT_HVC_GZVM_PROBE, the call gzvm_arch_probe() issues to find out whether
 * a hypervisor is installed at EL2.  Overridable at load time through the
 * hvc_fn module parameter.
 */
#define GZVM_HVC_PROBE_FN_DEFAULT	0

#define GZVM_HCALL_ID(fn)						\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_64,	\
			   ARM_SMCCC_OWNER_VENDOR_HYP, (fn))

/* The character device gzvm_drv_probe() registers. */
#define GZVM_DEVICE_NODE_DEFAULT	"/dev/gzvm"

/* Verdict codes, mirrored by cli/mgz.h. */
#define GZVM_VERDICT_SUPPORTED		0
#define GZVM_VERDICT_LIKELY		1
#define GZVM_VERDICT_UNSUPPORTED	2

/*
 * Candidate names for the driver probe entry point.  gzvm_drv_probe is what
 * every observed gzvm.ko exports to kallsyms; the remaining entries cover
 * kernels that renamed or split it.  Tried in order, first hit wins.
 */
#define GZVM_PROBE_CANDIDATE_LIST			\
	{						\
		"gzvm_drv_probe",			\
		"gzvm_drv_platform_probe",		\
		"gzvm_probe",				\
		NULL					\
	}

/*
 * Resolve a symbol address by name.  Local (non-exported) symbols cannot be
 * looked up through the normal module loader, but they are present in
 * kallsyms, which is exactly what kprobe resolution uses.
 */
static inline int gzvm_resolve_symbol(const char *name, void **out)
{
	struct kprobe kp = { .symbol_name = name };
	int ret;

	ret = register_kprobe(&kp);
	if (ret < 0)
		return ret;

	*out = (void *)kp.addr;
	unregister_kprobe(&kp);

	return *out ? 0 : -ENOENT;
}

static inline const char *gzvm_verdict_name(int verdict)
{
	switch (verdict) {
	case GZVM_VERDICT_SUPPORTED:
		return "supported";
	case GZVM_VERDICT_LIKELY:
		return "likely";
	default:
		return "unsupported";
	}
}

#endif /* _GZVM_COMMON_H */
