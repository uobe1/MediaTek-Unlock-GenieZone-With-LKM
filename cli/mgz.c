// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * MediaTek-Unlock-GenieZone-With-LKM — mgz command line interface.
 *
 * Commands: check | install | remove | ksu-keep-alive | help
 * Options : --help -h, --version -V, --show-debug-details -d,
 *           --quiet -q, --no-color -n
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mgz.h"

static struct mgz_opts g_opts = {
	.show_debug_details = 0,
	.quiet = 0,
	.color = 1,
};

void mgz_opts_set(const struct mgz_opts *opts)
{
	g_opts = *opts;
}

const struct mgz_opts *mgz_opts(void)
{
	return &g_opts;
}

static const char *level_prefix(enum mgz_log_level level)
{
	switch (level) {
	case MGZ_LOG_OK:
		return "[ ok ]";
	case MGZ_LOG_WARN:
		return "[warn]";
	case MGZ_LOG_ERR:
		return "[fail]";
	case MGZ_LOG_DEBUG:
		return "[dbg ]";
	default:
		return "[info]";
	}
}

static const char *level_color(enum mgz_log_level level)
{
	switch (level) {
	case MGZ_LOG_OK:
		return "\033[32m";
	case MGZ_LOG_WARN:
		return "\033[33m";
	case MGZ_LOG_ERR:
		return "\033[31m";
	case MGZ_LOG_DEBUG:
		return "\033[90m";
	default:
		return "";
	}
}

void mgz_log(enum mgz_log_level level, const char *fmt, ...)
{
	va_list ap;
	int color = g_opts.color && isatty(STDOUT_FILENO);

	if (g_opts.quiet && level != MGZ_LOG_ERR)
		return;
	if (level == MGZ_LOG_DEBUG && !g_opts.show_debug_details)
		return;

	if (level == MGZ_LOG_RAW) {
		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
		return;
	}

	if (color)
		fputs(level_color(level), stdout);

	fputs(level_prefix(level), stdout);
	fputs(" ", stdout);

	if (color)
		fputs("\033[0m", stdout);

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

	putchar('\n');
}

static const char *const usage_text =
	"mgz — MediaTek GenieZone unlock helper\n"
	"\n"
	"Usage:\n"
	"  mgz <command> [options] [module-path]\n"
	"\n"
	"Commands:\n"
	"  check              Detect whether this device can be unlocked and why\n"
	"  install            Load gzvm_unlock.ko once and create /dev/gzvm\n"
	"  remove             Unload gzvm_unlock.ko and drop the persisted module\n"
	"  ksu-keep-alive     Install a KernelSU module that reloads the .ko on boot\n"
	"  verity             ioctl /dev/gzvm to prove the driver actually works\n"
	"  help               Show this help\n"
	"\n"
	"Options:\n"
	"  -h, --help                Show this help\n"
	"  -V, --version             Show version\n"
	"  -d, --show-debug-details  Print every detection step and command output\n"
	"  -q, --quiet               Only print errors\n"
	"  -n, --no-color            Disable ANSI colors\n"
	"\n"
	"Notes:\n"
	"  * [module-path] is a .ko file or a directory containing gzvm_unlock.ko.\n"
	"    Default search order: argument, current directory, " MGZ_BUILD_DIR ",\n"
	"    /data/local/tmp, the directory of this executable.\n"
	"  * Root is obtained through `sudo`, falling back to `su -c`.\n"
	"  * ksud is only reachable through `su -c`, so KernelSU operations use it\n"
	"    even when ordinary commands run through `sudo`.\n"
	"  * ksu-keep-alive requires ksud; it writes both service.sh (standard\n"
	"    boot) and late-load.sh (late-load root solutions).\n"
	"\n"
	"Documentation: docs/en (default) and docs/zh-CN.\n";

void mgz_usage(const char *program)
{
	printf("Usage: %s <command> [options] [module-path]\n\n", program);
	fputs(usage_text, stdout);
}

void mgz_version(const char *program)
{
	printf("%s %s (%s)\n", program, MGZ_VERSION, MGZ_MODULE_NAME);
	puts("License GPLv3-or-later");
}

#define MGZ_MAX_POSITIONAL 8

int main(int argc, char **argv)
{
	struct mgz_root root;
	const char *program = "mgz";
	char *positional[MGZ_MAX_POSITIONAL];
	int npos = 0;
	int want_help = 0, want_version = 0;
	int i, rc;

	if (argc > 0 && argv[0] && *argv[0])
		program = argv[0];

	/* Options may appear anywhere; everything else is positional. */
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
			want_help = 1;
		else if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version"))
			want_version = 1;
		else if (!strcmp(argv[i], "-d") ||
			 !strcmp(argv[i], "--show-debug-details"))
			g_opts.show_debug_details = 1;
		else if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "--quiet"))
			g_opts.quiet = 1;
		else if (!strcmp(argv[i], "-n") || !strcmp(argv[i], "--no-color"))
			g_opts.color = 0;
		else if (argv[i][0] == '-') {
			mgz_log(MGZ_LOG_ERR, "unknown option '%s'", argv[i]);
			mgz_usage(program);
			return 1;
		} else if (npos < MGZ_MAX_POSITIONAL) {
			positional[npos++] = argv[i];
		}
	}

	if (want_version) {
		mgz_version(program);
		return 0;
	}

	if (want_help || npos == 0) {
		mgz_usage(program);
		return want_help ? 0 : 1;
	}

	if (!strcmp(positional[0], "help")) {
		mgz_usage(program);
		return 0;
	}

	if (mgz_root_init(&root) < 0) {
		mgz_log(MGZ_LOG_ERR, "cannot determine the root environment");
		return 1;
	}

	if (!strcmp(positional[0], "check"))
		rc = cmd_check(npos - 1, positional + 1, &root);
	else if (!strcmp(positional[0], "install"))
		rc = cmd_install(npos - 1, positional + 1, &root);
	else if (!strcmp(positional[0], "remove"))
		rc = cmd_remove(npos - 1, positional + 1, &root);
	else if (!strcmp(positional[0], "ksu-keep-alive"))
		rc = cmd_ksu_keep_alive(npos - 1, positional + 1, &root);
	else if (!strcmp(positional[0], "verity"))
		rc = cmd_verity(npos - 1, positional + 1, &root);
	else {
		mgz_log(MGZ_LOG_ERR, "unknown command '%s'", positional[0]);
		mgz_usage(program);
		return 1;
	}

	return rc ? 1 : 0;
}
