// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * mgz verity — prove that /dev/gzvm actually works, not just that it exists.
 *
 * The node can be present while the hypervisor is gone; only an ioctl can
 * tell the difference.
 *
 *   GZVM_CHECK_EXTENSION  ask the driver for a capability; a dead hypervisor
 *                         surfaces as -EOPNOTSUPP/-ENODEV here
 *   GZVM_CREATE_VM        the definitive test: create a VM and close it
 *
 * Definitions mirror include/uapi/linux/gzvm.h (GZVM_IOC_MAGIC 0x92).
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "mgz.h"

#define GZVM_IOC_MAGIC 0x92 /* 'gz' */

/* ioctls for /dev/gzvm fds */
#define GZVM_CREATE_VM _IO(GZVM_IOC_MAGIC, 0x01)
#define GZVM_CHECK_EXTENSION _IO(GZVM_IOC_MAGIC, 0x03)

#define GZVM_CAP_VM_GPA_SIZE 0xa5

static int gzvm_check_extension(int fd, unsigned long cap, long *out)
{
	unsigned long arg = cap;

	errno = 0;

	/* The driver get_user()s the capability out of the argument. */
	*out = ioctl(fd, GZVM_CHECK_EXTENSION, &arg);

	return *out < 0 ? -1 : 0;
}

int cmd_verity(int argc, char **argv, const struct mgz_root *root)
{
	const char *node = MGZ_DEFAULT_NODE;
	int fd;
	long ret;

	(void)argc;
	(void)argv;
	(void)root;

	/* A stale node is also a finding: report exactly what we see. */
	if (access(node, F_OK) != 0) {
		mgz_log(MGZ_LOG_ERR, "%s does not exist; run `mgz install`",
			node);
		return -1;
	}

	mgz_log(MGZ_LOG_INFO, "open %s", node);

	fd = open(node, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		mgz_log(MGZ_LOG_ERR, "cannot open %s: %s", node,
			strerror(errno));
		return -1;
	}

	/*
	 * Passive probe first: asking for a capability has no side effects.
	 * A driver that is alive but a hypervisor that is not answers
	 * -EOPNOTSUPP or -ENODEV here.
	 */
	if (gzvm_check_extension(fd, GZVM_CAP_VM_GPA_SIZE, &ret) == 0)
		mgz_log(MGZ_LOG_OK,
			"GZVM_CHECK_EXTENSION(GZVM_CAP_VM_GPA_SIZE) -> %ld",
			ret);
	else
		mgz_log(MGZ_LOG_WARN,
			"GZVM_CHECK_EXTENSION(GZVM_CAP_VM_GPA_SIZE) -> %s "
			"(%ld)", strerror(errno), ret);

	/*
	 * The definitive test: GZVM_CREATE_VM returns a VM fd. Whatever the
	 * errno, the node gets closed again immediately.
	 */
	errno = 0;
	ret = ioctl(fd, GZVM_CREATE_VM);

	if (ret >= 0) {
		mgz_log(MGZ_LOG_OK, "GZVM_CREATE_VM -> vm fd %ld", ret);
		mgz_log(MGZ_LOG_OK, "%s is alive; the hypervisor answers",
			node);
		close(ret);
		close(fd);
		return 0;
	}

	mgz_log(MGZ_LOG_ERR, "GZVM_CREATE_VM failed: %s", strerror(errno));
	switch (errno) {
	case ENODEV:
		mgz_log(MGZ_LOG_ERR,
			"the hypervisor is not reachable; GenieZone is most "
			"likely disabled below Linux (ATF/EL2)");
		break;
	case EOPNOTSUPP:
	case ENOTTY:
		mgz_log(MGZ_LOG_ERR,
			"the driver behind %s does not accept VM creation",
			node);
		break;
	default:
		break;
	}

	close(fd);

	return -1;
}
