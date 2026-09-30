// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * mgz check — decide whether this device can be unlocked, and say why.
 *
 * Everything that can be read from userspace is read from userspace; only the
 * HVC probe and the kallsyms resolution need kernel context, and those are
 * delegated to gzvm_probe.ko when it is available.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

#include "mgz.h"

static void copy_prop(const char *prop, char *out, size_t outsz)
{
	char cmd[256];
	char buf[256];

	snprintf(cmd, sizeof(cmd), "getprop %s", prop);
	if (mgz_sh(cmd, buf, sizeof(buf)) == 0 && buf[0] && buf[0] != '\n') {
		buf[strcspn(buf, "\r\n")] = '\0';
		snprintf(out, outsz, "%s", buf);
	}
}

static int dir_has_match(const char *dir, const char *needle)
{
	DIR *d;
	struct dirent *e;
	int found = 0;

	d = opendir(dir);
	if (!d)
		return 0;

	while ((e = readdir(d))) {
		char lower[256];
		size_t i;

		snprintf(lower, sizeof(lower), "%s", e->d_name);
		for (i = 0; lower[i]; i++)
			lower[i] = (char)tolower((unsigned char)lower[i]);

		if (strstr(lower, needle)) {
			found = 1;
			mgz_log(MGZ_LOG_DEBUG, "%s: matched %s", dir, e->d_name);
			break;
		}
	}

	closedir(d);
	return found;
}

static int file_has_symbol(const char *path, const char *symbol)
{
	FILE *fp;
	char line[512];
	int found = 0;

	fp = fopen(path, "r");
	if (!fp)
		return 0;

	while (fgets(line, sizeof(line), fp)) {
		/* kallsyms lines end with the symbol name, possibly [module]. */
		char *name = strrchr(line, ' ');

		if (!name)
			continue;
		name++;
		if (!strncmp(name, symbol, strlen(symbol))) {
			char *end = name + strlen(symbol);

			if (*end == '\0' || *end == '\n' || *end == '\t' ||
			    *end == ' ') {
				found = 1;
				break;
			}
		}
	}

	fclose(fp);
	return found;
}

static int module_loaded(const char *name)
{
	FILE *fp;
	char line[512];
	int found = 0;

	fp = fopen("/proc/modules", "r");
	if (!fp)
		return 0;

	while (fgets(line, sizeof(line), fp)) {
		char *sp = strchr(line, ' ');

		if (!sp)
			continue;
		*sp = '\0';
		if (!strcmp(line, name)) {
			found = 1;
			break;
		}
	}

	fclose(fp);
	return found;
}

static void detect_kernel(struct mgz_device *dev)
{
	struct utsname u;
	char *p;

	if (uname(&u) != 0)
		return;

	snprintf(dev->uname_release, sizeof(dev->uname_release), "%s", u.release);

	/* Kernel major version: the first two dot separated numbers. */
	snprintf(dev->kernel_major, sizeof(dev->kernel_major), "%s", u.release);
	p = strchr(dev->kernel_major, '.');
	if (p) {
		p = strchr(p + 1, '.');
		if (p)
			*p = '\0';
	}

	/* Android version: authoritative from the property, else from uname. */
	copy_prop("ro.build.version.release", dev->android_version,
		  sizeof(dev->android_version));
	if (!dev->android_version[0]) {
		p = strstr(u.release, "-android");
		if (p) {
			size_t i = 0;

			p += strlen("-android");
			while (p[i] && isdigit((unsigned char)p[i]) &&
			       i < sizeof(dev->android_version) - 1) {
				dev->android_version[i] = p[i];
				i++;
			}
			dev->android_version[i] = '\0';
		}
	}

	/* KMI generation, e.g. android16-6.12 — major versions only. */
	if (dev->android_version[0] && dev->kernel_major[0])
		snprintf(dev->kmi, sizeof(dev->kmi), "android%s-%s",
			 dev->android_version, dev->kernel_major);
}

static void detect_platform(struct mgz_device *dev)
{
	char buf[64];
	size_t i;

	copy_prop("ro.board.platform", dev->platform, sizeof(dev->platform));
	if (!dev->platform[0])
		copy_prop("ro.hardware", dev->platform, sizeof(dev->platform));
	if (!dev->platform[0])
		copy_prop("ro.mediatek.platform", dev->platform,
			  sizeof(dev->platform));
	if (!dev->platform[0])
		copy_prop("ro.product.board", dev->platform,
			  sizeof(dev->platform));

	copy_prop("ro.product.model", dev->model, sizeof(dev->model));

	snprintf(buf, sizeof(buf), "%s", dev->platform);
	for (i = 0; buf[i]; i++)
		buf[i] = (char)tolower((unsigned char)buf[i]);

	dev->is_mediatek = (!strncmp(buf, "mt", 2) || strstr(buf, "mediatek") ||
			    strstr(buf, "dimensity"));
}

static void detect_modules(struct mgz_device *dev)
{
	static const char *const paths[] = {
		"/system_dlkm/lib/modules/gzvm.ko",
		"/vendor_dlkm/lib/modules/gzvm.ko",
		"/vendor/lib/modules/gzvm.ko",
		"/odm/lib/modules/gzvm.ko",
		"/lib/modules/gzvm.ko",
	};
	size_t i;

	for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
		if (mgz_file_exists(paths[i])) {
			dev->gzvm_ko_present = 1;
			snprintf(dev->gzvm_ko_path, sizeof(dev->gzvm_ko_path),
				 "%s", paths[i]);
			break;
		}
	}

	dev->gzvm_ko_loaded = module_loaded("gzvm");

	/*
	 * kallsyms is world readable; kptr_restrict only hides the addresses,
	 * the symbol names are enough for what we need to know.
	 */
	dev->symbol_present = file_has_symbol("/proc/kallsyms", "gzvm_drv_probe");
	if (!dev->symbol_present)
		dev->symbol_present =
			file_has_symbol("/proc/kallsyms", "gzvm_arch_probe");
}

static void detect_config(struct mgz_device *dev, const struct mgz_root *root)
{
	char out[32768];
	char cmd[512];

	dev->cfg_modules = -1;
	dev->cfg_kallsyms_all = -1;
	dev->cfg_kprobes = -1;
	dev->cfg_sig_force = -1;

	snprintf(cmd, sizeof(cmd),
		 "zcat /proc/config.gz 2>/dev/null | grep -E "
		 "'CONFIG_MODULES=|CONFIG_MODULE_SIG_FORCE|"
		 "CONFIG_KALLSYMS_ALL=|CONFIG_KPROBES=|CONFIG_IKCONFIG'");

	if (mgz_root_exec(root, cmd, out, sizeof(out)) == 0 && out[0]) {
		dev->cfg_available = 1;
		dev->cfg_modules = strstr(out, "CONFIG_MODULES=y") != NULL;
		dev->cfg_kallsyms_all = strstr(out, "CONFIG_KALLSYMS_ALL=y") != NULL;
		dev->cfg_kprobes = strstr(out, "CONFIG_KPROBES=y") != NULL ||
				   strstr(out, "CONFIG_KPROBES=m") != NULL;
		dev->cfg_sig_force = strstr(out, "CONFIG_MODULE_SIG_FORCE=y") != NULL;
	}

	{
		char buf[32];

		if (mgz_read_file("/proc/sys/kernel/modules_disabled", buf,
				  sizeof(buf)) == 0)
			dev->modules_disabled = atoi(buf);
	}
}

static void detect_firmware(struct mgz_device *dev)
{
	dev->gz_partitions = dir_has_match("/dev/block/by-name", "gz");
	dev->gz_reserved_mem =
		dir_has_match("/proc/device-tree/reserved-memory", "gz");
	dev->dt_geniezone_node = dir_has_match("/proc/device-tree", "genie");

	if (!dev->dt_geniezone_node)
		dev->dt_geniezone_node = dir_has_match("/proc/device-tree", "gz");
}

static int read_sysfs_value(const char *module, const char *param,
			    char *out, size_t outsz)
{
	char path[512];

	snprintf(path, sizeof(path), MGZ_SYSFS_PARAM_FMT, module, param);
	if (mgz_read_file(path, out, outsz) != 0)
		return -1;

	out[strcspn(out, "\r\n")] = '\0';

	return 0;
}

static int read_sysfs_int(const char *module, const char *param, int *out)
{
	char buf[64];

	if (read_sysfs_value(module, param, buf, sizeof(buf)) != 0)
		return -1;

	*out = atoi(buf);

	return 0;
}

static void detect_el2(struct mgz_device *dev, const struct mgz_root *root)
{
	char module_path[512];
	char cmd[1024];
	char quoted[512];
	char out[2048];
	int value;

	dev->el2_alive = -1;

	if (!mgz_find_module_file(NULL, MGZ_PROBE_KO, module_path,
				  sizeof(module_path))) {
		mgz_log(MGZ_LOG_DEBUG,
			"%s not found, skipping the EL2 HVC probe",
			MGZ_PROBE_KO);
		return;
	}

	mgz_shell_quote(module_path, quoted, sizeof(quoted));
	snprintf(cmd, sizeof(cmd), "insmod %s", quoted);
	if (mgz_root_exec(root, cmd, out, sizeof(out)) != 0) {
		mgz_log(MGZ_LOG_DEBUG, "insmod %s failed: %s", module_path, out);
		return;
	}

	if (read_sysfs_int("gzvm_probe", "el2_alive", &value) == 0)
		dev->el2_alive = value;
	if (read_sysfs_int("gzvm_probe", "symbol_found", &value) == 0)
		dev->probe_symbol_found = value;

	{
		char buf[64];

		if (read_sysfs_value("gzvm_probe", "hvc_a0", buf,
				     sizeof(buf)) == 0)
			dev->hvc_a0 = strtoul(buf, NULL, 0);
	}

	snprintf(cmd, sizeof(cmd), "rmmod gzvm_probe");
	mgz_root_exec(root, cmd, out, sizeof(out));
}

/*
 * Read the vermagic string out of a .ko. KMI stability is guaranteed per
 * kernel major version, and the DDK build directory is built from its own
 * 6.12.y point release, so the major version is what has to match -- the
 * full release string is not required to be identical.
 */
static int read_vermagic(const char *path, char *out, size_t outsz)
{
	FILE *fp;
	char *buf;
	char *found;
	long size;
	size_t i = 0;

	fp = fopen(path, "rb");
	if (!fp)
		return 0;

	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return 0;
	}

	size = ftell(fp);
	if (size <= 0) {
		fclose(fp);
		return 0;
	}

	rewind(fp);
	buf = malloc((size_t)size);
	if (!buf) {
		fclose(fp);
		return 0;
	}

	if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
		free(buf);
		fclose(fp);
		return 0;
	}

	found = memmem(buf, (size_t)size, "vermagic=", strlen("vermagic="));
	if (found) {
		found += strlen("vermagic=");
		for (; i + 1 < outsz && *found && *found != ' ' &&
		       *found != '\n'; found++)
			out[i++] = *found;
		out[i] = '\0';
	}

	free(buf);
	fclose(fp);

	return i > 0;
}

static void detect_built_module(struct mgz_device *dev)
{
	char path[512];
	char vermagic[128];
	char major[16];
	char *p;

	if (!mgz_find_module_file(NULL, MGZ_UNLOCK_KO, path, sizeof(path))) {
		mgz_log(MGZ_LOG_DEBUG, "%s not found", MGZ_UNLOCK_KO);
		return;
	}

	snprintf(dev->module_path, sizeof(dev->module_path), "%s", path);

	if (!read_vermagic(path, vermagic, sizeof(vermagic)))
		return;

	snprintf(dev->module_vermagic, sizeof(dev->module_vermagic), "%s",
		 vermagic);

	snprintf(major, sizeof(major), "%s", vermagic);
	p = strchr(major, '.');
	if (p) {
		p = strchr(p + 1, '.');
		if (p)
			*p = '\0';
	}

	dev->built_module_matches = !strcmp(major, dev->kernel_major);
}

int mgz_detect(struct mgz_device *dev, const struct mgz_root *root)
{
	memset(dev, 0, sizeof(*dev));
	dev->el2_alive = -1;
	dev->modules_disabled = -1;

	detect_kernel(dev);
	detect_platform(dev);
	detect_modules(dev);
	detect_config(dev, root);
	detect_firmware(dev);
	detect_el2(dev, root);
	detect_built_module(dev);

	return 0;
}

static void print_row(const char *key, const char *fmt, ...)
{
	va_list ap;

	mgz_log(MGZ_LOG_RAW, "  %-18s", key);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
}

static void print_bool_row(const char *key, int value)
{
	print_row(key, "%s", value > 0 ? "yes" : value == 0 ? "no" : "unknown");
}

int cmd_check(int argc, char **argv, const struct mgz_root *root)
{
	struct mgz_device dev;
	int verdict;

	(void)argc;
	(void)argv;

	mgz_log(MGZ_LOG_INFO, "collecting device facts");
	mgz_detect(&dev, root);

	mgz_log(MGZ_LOG_RAW, "\nplatform\n");
	print_row("model", "%s", dev.model[0] ? dev.model : "-");
	print_row("platform", "%s", dev.platform[0] ? dev.platform : "-");
	print_row("mediatek", "%s", dev.is_mediatek ? "yes" : "no");

	mgz_log(MGZ_LOG_RAW, "\nkernel\n");
	print_row("release", "%s", dev.uname_release);
	print_row("android", "%s", dev.android_version[0] ?
				   dev.android_version : "-");
	print_row("kmi", "%s", dev.kmi[0] ? dev.kmi : "-");
	print_bool_row("modules", dev.cfg_modules);
	print_bool_row("kallsyms_all", dev.cfg_kallsyms_all);
	print_bool_row("kprobes", dev.cfg_kprobes);
	print_bool_row("sig_force", dev.cfg_sig_force);
	print_row("modules_disabled", "%d", dev.modules_disabled);

	mgz_log(MGZ_LOG_RAW, "\nhypervisor\n");
	print_bool_row("gzvm.ko file", dev.gzvm_ko_present);
	print_row("gzvm.ko path", "%s", dev.gzvm_ko_path[0] ?
					 dev.gzvm_ko_path : "-");
	print_bool_row("gzvm loaded", dev.gzvm_ko_loaded);
	print_bool_row("probe symbol", dev.symbol_present);
	if (dev.el2_alive == 1)
		print_row("EL2 HVC", "alive (a0=0)");
	else if (dev.el2_alive == 0)
		print_row("EL2 HVC", "no response (a0=0x%lx)", dev.hvc_a0);
	else
		print_row("EL2 HVC", "not probed");

	mgz_log(MGZ_LOG_RAW, "\nfirmware\n");
	print_bool_row("gz partitions", dev.gz_partitions);
	print_bool_row("gz reserved mem", dev.gz_reserved_mem);
	print_bool_row("dt geniezone", dev.dt_geniezone_node);

	mgz_log(MGZ_LOG_RAW, "\nmodule\n");
	print_row("unlock module", "%s", dev.module_path[0] ?
					 dev.module_path : "not found");
	print_row("vermagic", "%s", dev.module_vermagic[0] ?
				    dev.module_vermagic : "-");
	print_row("matches this kmi", "%s",
		  dev.module_vermagic[0] ?
		  (dev.built_module_matches ? "yes" : "NO") : "-");

	if (dev.el2_alive == 1 && dev.symbol_present && dev.gzvm_ko_loaded)
		verdict = MGZ_VERDICT_SUPPORTED;
	else if (dev.symbol_present || dev.gz_partitions || dev.gz_reserved_mem)
		verdict = MGZ_VERDICT_LIKELY;
	else
		verdict = MGZ_VERDICT_UNSUPPORTED;

	mgz_log(MGZ_LOG_RAW, "");
	switch (verdict) {
	case MGZ_VERDICT_SUPPORTED:
		mgz_log(MGZ_LOG_OK, "verdict: SUPPORTED — run `mgz install`");
		break;
	case MGZ_VERDICT_LIKELY:
		mgz_log(MGZ_LOG_WARN,
			"verdict: LIKELY — build the modules for KMI %s and "
			"re-run check", dev.kmi[0] ? dev.kmi : "unknown");
		break;
	default:
		mgz_log(MGZ_LOG_ERR, "verdict: UNSUPPORTED");
		break;
	}

	if (root->uid != 0)
		mgz_log(MGZ_LOG_WARN, "not running as root, privileged checks "
				      "were skipped");

	return verdict == MGZ_VERDICT_UNSUPPORTED ? 1 : 0;
}
