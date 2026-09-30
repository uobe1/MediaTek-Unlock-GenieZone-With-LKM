// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Privilege handling and small filesystem helpers.
 *
 * Root strategy, in the order the project recommends it:
 *   sudo  ->  su -c
 *
 * ksud lives in /data/adb/ksu/bin, which is normally only on the PATH of a
 * `su -c` shell, so KernelSU operations go through `su -c` even when ordinary
 * commands run through `sudo`.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "mgz.h"

static int run_popen(const char *cmd, char *out, size_t outsz)
{
	FILE *fp;
	size_t len = 0;
	int status;

	mgz_log(MGZ_LOG_DEBUG, "exec: %s", cmd);

	fp = popen(cmd, "r");
	if (!fp)
		return -1;

	if (out && outsz) {
		out[0] = '\0';
		while (len + 1 < outsz && fgets(out + len, outsz - len, fp))
			len += strlen(out + len);
		/* Drain whatever did not fit. */
		while (!feof(fp) && fgetc(fp) != EOF)
			;
	} else {
		while (!feof(fp) && fgetc(fp) != EOF)
			;
	}

	status = pclose(fp);
	if (WIFEXITED(status))
		return WEXITSTATUS(status);

	return -1;
}

int mgz_sh(const char *cmd, char *out, size_t outsz)
{
	char buf[2048];

	snprintf(buf, sizeof(buf), "%s 2>&1", cmd);

	return run_popen(buf, out, outsz);
}

int mgz_root_init(struct mgz_root *root)
{
	char out[512];

	memset(root, 0, sizeof(*root));
	root->uid = (int)getuid();

	if (mgz_sh("command -v sudo", out, sizeof(out)) == 0 && out[0] == '/')
		root->sudo = 1;

	if (mgz_sh("command -v su", out, sizeof(out)) == 0 && out[0] == '/')
		root->su = 1;

	/*
	 * ksud is visible from a `su -c` shell even when sudo cannot see it.
	 * Fall back to the well known absolute path.
	 */
	if (root->su) {
		char cmd[256];

		snprintf(cmd, sizeof(cmd), "su -c 'command -v ksud'");
		if (mgz_sh(cmd, out, sizeof(out)) == 0 && out[0] == '/') {
			out[strcspn(out, "\r\n")] = '\0';
			snprintf(root->ksud_path, sizeof(root->ksud_path),
				 "%s", out);
			root->ksud = 1;
		}
	}

	if (!root->ksud && mgz_file_exists(MGZ_KSU_BIN)) {
		snprintf(root->ksud_path, sizeof(root->ksud_path), "%s",
			 MGZ_KSU_BIN);
		root->ksud = 1;
	}

	if (root->uid != 0 && !root->sudo && !root->su) {
		mgz_log(MGZ_LOG_ERR, "no root: neither sudo nor su is available");
		return -1;
	}

	mgz_log(MGZ_LOG_DEBUG, "uid=%d sudo=%d su=%d ksud=%d (%s)",
		root->uid, root->sudo, root->su, root->ksud,
		root->ksud ? root->ksud_path : "-");

	return 0;
}

int mgz_root_exec(const struct mgz_root *root, const char *cmd,
		  char *out, size_t outsz)
{
	char quoted[1024];
	char buf[1536];
	int rc;

	if (out && outsz)
		out[0] = '\0';

	/* Already root: run it directly, no wrapper needed. */
	if (root->uid == 0)
		return mgz_sh(cmd, out, outsz);

	mgz_shell_quote(cmd, quoted, sizeof(quoted));

	/* sudo is the recommended path; su -c is the compatibility fallback. */
	if (root->sudo) {
		snprintf(buf, sizeof(buf), "sudo sh -c %s", quoted);
		rc = mgz_sh(buf, out, outsz);
		if (rc == 0)
			return 0;
	}

	if (root->su) {
		snprintf(buf, sizeof(buf), "su -c %s", quoted);
		return mgz_sh(buf, out, outsz);
	}

	return -1;
}

int mgz_root_exec_ksud(const struct mgz_root *root, const char *args,
		       char *out, size_t outsz)
{
	char cmd[1024];

	if (!root->ksud) {
		mgz_log(MGZ_LOG_ERR, "ksud not found; KernelSU is required here");
		return -1;
	}

	/* ksud is reachable from a `su -c` shell; sudo rarely sees it. */
	if (root->su && root->uid != 0)
		snprintf(cmd, sizeof(cmd), "su -c '%s %s'", root->ksud_path, args);
	else if (root->uid != 0)
		snprintf(cmd, sizeof(cmd), "sudo %s %s", root->ksud_path, args);
	else
		snprintf(cmd, sizeof(cmd), "%s %s", root->ksud_path, args);

	return mgz_sh(cmd, out, outsz);
}

void mgz_shell_quote(const char *in, char *out, size_t outsz)
{
	size_t o = 0;

	if (outsz < 4)
		return;

	out[o++] = '\'';
	for (; *in && o + 3 < outsz; in++) {
		if (*in == '\'') {
			out[o++] = '\'';
			out[o++] = '\\';
			out[o++] = '\'';
			out[o++] = '\'';
		} else {
			out[o++] = *in;
		}
	}
	out[o++] = '\'';
	out[o] = '\0';
}

int mgz_file_exists(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int mgz_dir_exists(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int mgz_mkdirs(const char *path)
{
	char tmp[512];
	size_t len;
	char *p;

	if (mgz_dir_exists(path))
		return 0;

	snprintf(tmp, sizeof(tmp), "%s", path);
	len = strlen(tmp);
	if (len == 0)
		return -1;
	if (tmp[len - 1] == '/')
		tmp[len - 1] = '\0';

	for (p = tmp + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (!mgz_dir_exists(tmp) && mkdir(tmp, 0755) != 0 && errno != EEXIST)
			return -1;
		*p = '/';
	}

	return mkdir(tmp, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

int mgz_copy_file(const char *src, const char *dst)
{
	FILE *in, *out;
	char buf[8192];
	size_t n;

	in = fopen(src, "rb");
	if (!in)
		return -1;

	out = fopen(dst, "wb");
	if (!out) {
		fclose(in);
		return -1;
	}

	while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
		fwrite(buf, 1, n, out);

	fclose(in);
	fclose(out);
	chmod(dst, 0644);

	return 0;
}

int mgz_write_file(const char *path, const char *content, mode_t mode)
{
	FILE *fp;

	fp = fopen(path, "w");
	if (!fp)
		return -1;

	fputs(content, fp);
	fclose(fp);
	chmod(path, mode);

	return 0;
}

int mgz_read_file(const char *path, char *out, size_t outsz)
{
	FILE *fp;
	size_t len;

	fp = fopen(path, "r");
	if (!fp)
		return -1;

	out[0] = '\0';
	len = fread(out, 1, outsz - 1, fp);
	out[len] = '\0';
	fclose(fp);

	return 0;
}

int mgz_rmtree(const char *path)
{
	char cmd[1024];
	char quoted[512];

	mgz_shell_quote(path, quoted, sizeof(quoted));
	snprintf(cmd, sizeof(cmd), "rm -rf %s", quoted);

	return system(cmd) == 0 ? 0 : -1;
}

/*
 * Locate a .ko file.  Search order: explicit hint (file or directory),
 * current directory, the build drop directory, /data/local/tmp, and the
 * directory the executable itself lives in.
 */
const char *mgz_find_module_file(const char *hint, const char *name,
				 char *out, size_t outsz)
{
	static const char *const dirs[] = {
		MGZ_BUILD_DIR,
		"/data/local/tmp",
		".",
	};
	char path[512];
	size_t i;

	if (hint && *hint) {
		struct stat st;

		if (stat(hint, &st) == 0) {
			if (S_ISDIR(st.st_mode)) {
				snprintf(path, sizeof(path), "%s/%s", hint, name);
				if (mgz_file_exists(path)) {
					snprintf(out, outsz, "%s", path);
					return out;
				}
			} else {
				snprintf(out, outsz, "%s", hint);
				return out;
			}
		}
	}

	for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
		snprintf(path, sizeof(path), "%s/%s", dirs[i], name);
		if (mgz_file_exists(path)) {
			snprintf(out, outsz, "%s", path);
			return out;
		}
	}

	/* Directory of the running executable. */
	{
		char exe[512];
		ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);

		if (n > 0) {
			char *slash;

			exe[n] = '\0';
			slash = strrchr(exe, '/');
			if (slash) {
				*slash = '\0';
				snprintf(path, sizeof(path), "%s/%s", exe, name);
				if (mgz_file_exists(path)) {
					snprintf(out, outsz, "%s", path);
					return out;
				}
			}
		}
	}

	return NULL;
}
