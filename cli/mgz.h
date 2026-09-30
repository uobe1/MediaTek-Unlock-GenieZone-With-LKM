/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * MediaTek-Unlock-GenieZone-With-LKM — userspace CLI (mgz)
 */
#ifndef MGZ_H
#define MGZ_H

#define MGZ_VERSION		"0.1.0"

#define MGZ_MODULE_ID		"mgz_unlock"
#define MGZ_MODULE_NAME		"MediaTek GenieZone Unlock"

/* KernelSU layout, see https://kernelsu.org/guide/module.html */
#define MGZ_KSU_MODULES_DIR	"/data/adb/modules"
#define MGZ_KSU_BIN		"/data/adb/ksu/bin/ksud"

#define MGZ_DEFAULT_NODE	"/dev/gzvm"
#define MGZ_PROBE_KO		"gzvm_probe.ko"
#define MGZ_UNLOCK_KO		"gzvm_unlock.ko"

/* Where freshly built modules usually land on a device. */
#define MGZ_BUILD_DIR		"/data/local/tmp/lkm_build"

#define MGZ_SYSFS_PARAM_FMT	"/sys/module/%s/parameters/%s"

enum mgz_log_level {
	MGZ_LOG_RAW,
	MGZ_LOG_INFO,
	MGZ_LOG_OK,
	MGZ_LOG_WARN,
	MGZ_LOG_ERR,
	MGZ_LOG_DEBUG,
};

/* Verdict codes shared with kernel/gzvm_common.h. */
#define MGZ_VERDICT_SUPPORTED	0
#define MGZ_VERDICT_LIKELY	1
#define MGZ_VERDICT_UNSUPPORTED	2

struct mgz_opts {
	int show_debug_details;
	int quiet;
	int color;
};

/* ---- cli/mgz.c ---------------------------------------------------------- */
void mgz_opts_set(const struct mgz_opts *opts);
const struct mgz_opts *mgz_opts(void);
void mgz_log(enum mgz_log_level level, const char *fmt, ...);
void mgz_usage(const char *program);
void mgz_version(const char *program);

/* ---- cli/root.c --------------------------------------------------------- */
struct mgz_root {
	int sudo;		/* `sudo` is available */
	int su;			/* `su` is available */
	int ksud;		/* ksud was located */
	char ksud_path[256];
	int uid;		/* getuid() result */
};

int mgz_sh(const char *cmd, char *out, size_t outsz);
int mgz_root_init(struct mgz_root *root);
int mgz_root_exec(const struct mgz_root *root, const char *cmd,
		  char *out, size_t outsz);
int mgz_root_exec_ksud(const struct mgz_root *root, const char *args,
		       char *out, size_t outsz);
int mgz_file_exists(const char *path);
int mgz_dir_exists(const char *path);
int mgz_copy_file(const char *src, const char *dst);
int mgz_write_file(const char *path, const char *content, mode_t mode);
int mgz_read_file(const char *path, char *out, size_t outsz);
int mgz_mkdirs(const char *path);
int mgz_rmtree(const char *path);
void mgz_shell_quote(const char *in, char *out, size_t outsz);
const char *mgz_find_module_file(const char *hint, const char *name,
				 char *out, size_t outsz);

/* ---- cli/detect.c ------------------------------------------------------- */
struct mgz_device {
	char uname_release[256];
	char kernel_major[16];
	char android_version[16];
	char kmi[32];
	char platform[64];
	char model[64];
	int is_mediatek;
	int gzvm_ko_present;
	int gzvm_ko_loaded;
	char gzvm_ko_path[256];
	int symbol_present;
	int cfg_modules;
	int cfg_kallsyms_all;
	int cfg_kprobes;
	int cfg_sig_force;
	int cfg_available;
	int gz_partitions;
	int gz_reserved_mem;
	int dt_geniezone_node;
	int modules_disabled;
	int el2_alive;		/* -1 unknown, 0 no, 1 yes */
	int probe_symbol_found;
	unsigned long hvc_a0;
	int built_module_matches;
	char module_path[256];
};

int mgz_detect(struct mgz_device *dev, const struct mgz_root *root);
int cmd_check(int argc, char **argv, const struct mgz_root *root);

/* ---- cli/ksu.c ---------------------------------------------------------- */
int cmd_install(int argc, char **argv, const struct mgz_root *root);
int cmd_remove(int argc, char **argv, const struct mgz_root *root);
int cmd_ksu_keep_alive(int argc, char **argv, const struct mgz_root *root);

#endif /* MGZ_H */
