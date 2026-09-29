/*
 * applications/selinux/selinux.c
 *
 * Multicall Android Desktop / SELinux userland tool suite for SIX:
 *   - getenforce   : Display current SELinux mode (Enforcing / Permissive)
 *   - setenforce   : Set SELinux mode (0 / 1 / Permissive / Enforcing)
 *   - chcon        : Change file SELinux security context
 *   - restorecon   : Restore file(s) default SELinux security contexts
 *   - runcon       : Run command in specified SELinux security context
 *   - load_policy  : Compile/verify & load 3-tier .te policy (with neverallow
 *                    and 3-tier visibility checks) into /sys/fs/selinux/load
 *   - audit2allow  : Translate kernel AVC denials into .te allow/allowxperm rules
 *   - seinfo       : Query policy statistics, types, tiers, and neverallows
 *   - sestatus     : Display detailed SELinux status (/proc/selinux)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *prog_basename(const char *path)
{
	const char *slash = strrchr(path, '/');
	return slash ? (slash + 1) : path;
}

static int read_text_file(const char *path, char *buf, int maxlen)
{
	int fd, n;
	if (!buf || maxlen <= 0)
		return -1;
	buf[0] = '\0';
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	n = read(fd, buf, maxlen - 1);
	close(fd);
	if (n < 0)
		return -1;
	buf[n] = '\0';
	return n;
}

static int get_file_context(const char *path, char *out, int maxlen)
{
	int fd, n;
	char cmd[160];

	out[0] = '\0';
	fd = open("/sys/fs/selinux/context", O_RDWR);
	if (fd < 0)
		return -1;
	sprintf(cmd, "get %s", path);
	if (write(fd, cmd, strlen(cmd)) < 0) {
		close(fd);
		return -1;
	}
	lseek(fd, 0, SEEK_SET);
	n = read(fd, out, maxlen - 1);
	close(fd);
	if (n <= 0)
		return -1;
	out[n] = '\0';
	while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r'))
		out[--n] = '\0';
	return 0;
}

/* -------------------------------------------------------------------
 * getenforce
 * ------------------------------------------------------------------- */
static int do_getenforce(int argc, char **argv)
{
	char buf[32];
	(void)argc;
	(void)argv;
	if (read_text_file("/sys/fs/selinux/enforce", buf, sizeof(buf)) < 0) {
		printf("Disabled\n");
		return 0;
	}
	if (buf[0] == '1')
		printf("Enforcing\n");
	else
		printf("Permissive\n");
	return 0;
}

/* -------------------------------------------------------------------
 * setenforce
 * ------------------------------------------------------------------- */
static int do_setenforce(int argc, char **argv)
{
	const char *val = NULL;
	int fd, rc;

	if (argc < 2) {
		fprintf(stderr, "usage:  setenforce [ Enforcing | Permissive | 1 | 0 ]\n");
		return 1;
	}
	if (!strcmp(argv[1], "1") || !strcmp(argv[1], "Enforcing") || !strcmp(argv[1], "enforcing")) {
		val = "1";
	} else if (!strcmp(argv[1], "0") || !strcmp(argv[1], "Permissive") || !strcmp(argv[1], "permissive")) {
		val = "0";
	} else {
		fprintf(stderr, "usage:  setenforce [ Enforcing | Permissive | 1 | 0 ]\n");
		return 1;
	}

	fd = open("/sys/fs/selinux/enforce", O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "setenforce: could not open /sys/fs/selinux/enforce: %s\n",
			strerror(errno));
		return 1;
	}
	rc = write(fd, val, 1);
	if (rc < 0) {
		fprintf(stderr, "setenforce:  setenforce() failed (%s)\n", strerror(errno));
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}

/* -------------------------------------------------------------------
 * chcon
 * ------------------------------------------------------------------- */
static int chcon_one(const char *ctx, const char *path, int verbose)
{
	int fd, rc;
	char cmd[256];

	fd = open("/sys/fs/selinux/context", O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "chcon: SELinux not enabled\n");
		return 1;
	}
	if (verbose)
		printf("changing security context of '%s' to '%s'\n", path, ctx);
	sprintf(cmd, "chcon %s %s", ctx, path);
	rc = write(fd, cmd, strlen(cmd));
	if (rc < 0) {
		fprintf(stderr, "chcon: failed to change context of '%s' to '%s': %s\n",
			path, ctx, strerror(errno));
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}

static int do_chcon(int argc, char **argv)
{
	int i = 1;
	int verbose = 0;
	const char *type_only = NULL;
	char full_ctx[64];
	const char *ctx;
	int ret = 0;

	while (i < argc && argv[i][0] == '-') {
		if (!strcmp(argv[i], "-v")) {
			verbose = 1;
			i++;
		} else if (!strcmp(argv[i], "-R")) {
			i++;
		} else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
			type_only = argv[i + 1];
			i += 2;
		} else {
			break;
		}
	}

	if (type_only) {
		sprintf(full_ctx, "u:object_r:%s:s0", type_only);
		ctx = full_ctx;
	} else {
		if (i >= argc) {
			fprintf(stderr, "usage: chcon [-v] [-t type] CONTEXT FILE...\n");
			return 1;
		}
		ctx = argv[i++];
		if (!strchr(ctx, ':')) {
			sprintf(full_ctx, "u:object_r:%s:s0", ctx);
			ctx = full_ctx;
		}
	}

	if (i >= argc) {
		fprintf(stderr, "usage: chcon [-v] [-t type] CONTEXT FILE...\n");
		return 1;
	}

	for (; i < argc; i++) {
		if (chcon_one(ctx, argv[i], verbose) != 0)
			ret = 1;
	}
	return ret;
}

/* -------------------------------------------------------------------
 * restorecon
 * ------------------------------------------------------------------- */
static int restorecon_path(const char *path, int recursive, int verbose)
{
	char old_ctx[64], new_ctx[64], cmd[256];
	int fd, rc;
	struct stat st;

	old_ctx[0] = '\0';
	get_file_context(path, old_ctx, sizeof(old_ctx));

	fd = open("/sys/fs/selinux/context", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "restorecon: SELinux not enabled\n");
		return 1;
	}
	sprintf(cmd, "restorecon %s", path);
	rc = write(fd, cmd, strlen(cmd));
	if (rc < 0) {
		fprintf(stderr, "restorecon: failed on '%s': %s\n", path, strerror(errno));
		close(fd);
		return 1;
	}
	lseek(fd, 0, SEEK_SET);
	new_ctx[0] = '\0';
	rc = read(fd, new_ctx, sizeof(new_ctx) - 1);
	if (rc > 0)
		new_ctx[rc] = '\0';
	close(fd);

	if (verbose) {
		printf("SELinux:  Relabeling %s from %s to %s.\n",
		       path,
		       old_ctx[0] ? old_ctx : "u:object_r:unlabeled:s0",
		       new_ctx[0] ? new_ctx : "u:object_r:system_file:s0");
	}

	if (recursive && stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
		DIR *d = opendir(path);
		if (d) {
			struct dirent *de;
			while ((de = readdir(d)) != NULL) {
				char child[256];
				if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
					continue;
				if (!strcmp(path, "/"))
					sprintf(child, "/%s", de->d_name);
				else
					sprintf(child, "%s/%s", path, de->d_name);
				restorecon_path(child, 1, verbose);
			}
			closedir(d);
		}
	}
	return 0;
}

static int do_restorecon(int argc, char **argv)
{
	int i = 1;
	int recursive = 0;
	int verbose = 0;
	int ret = 0;

	while (i < argc && argv[i][0] == '-') {
		if (!strcmp(argv[i], "-R") || !strcmp(argv[i], "-r")) {
			recursive = 1;
			i++;
		} else if (!strcmp(argv[i], "-v")) {
			verbose = 1;
			i++;
		} else if (!strcmp(argv[i], "-Rv") || !strcmp(argv[i], "-vR")) {
			recursive = 1;
			verbose = 1;
			i++;
		} else {
			break;
		}
	}

	if (i >= argc) {
		fprintf(stderr, "usage: restorecon [-R] [-v] pathname...\n");
		return 1;
	}
	if (verbose) {
		printf("SELinux: Loaded file_contexts from:\n"
		       "  /system/etc/selinux/plat_file_contexts\n"
		       "  /vendor/etc/selinux/vendor_file_contexts\n");
	}
	for (; i < argc; i++) {
		if (restorecon_path(argv[i], recursive, verbose) != 0)
			ret = 1;
	}
	return ret;
}

/* -------------------------------------------------------------------
 * runcon
 * ------------------------------------------------------------------- */
static int do_runcon(int argc, char **argv)
{
	int i = 1;
	const char *type_only = NULL;
	char full_ctx[64];
	const char *ctx = NULL;
	int fd;

	if (argc == 1) {
		char cur[64];
		if (read_text_file("/proc/self/attr/current", cur, sizeof(cur)) >= 0) {
			printf("%s\n", cur);
			return 0;
		}
	}

	while (i < argc && argv[i][0] == '-') {
		if (!strcmp(argv[i], "-t") && i + 1 < argc) {
			type_only = argv[i + 1];
			i += 2;
		} else if (!strcmp(argv[i], "-u") && i + 1 < argc) {
			i += 2;
		} else if (!strcmp(argv[i], "-r") && i + 1 < argc) {
			i += 2;
		} else {
			break;
		}
	}

	if (type_only) {
		sprintf(full_ctx, "u:r:%s:s0", type_only);
		ctx = full_ctx;
	} else {
		if (i >= argc) {
			fprintf(stderr, "usage: runcon [-t TYPE] CONTEXT COMMAND [ARGS...]\n");
			return 1;
		}
		ctx = argv[i++];
		if (!strchr(ctx, ':')) {
			sprintf(full_ctx, "u:r:%s:s0", ctx);
			ctx = full_ctx;
		}
	}

	if (i >= argc) {
		fprintf(stderr, "usage: runcon [-t TYPE] CONTEXT COMMAND [ARGS...]\n");
		return 1;
	}

	fd = open("/proc/self/attr/current", O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "runcon: cannot open /proc/self/attr/current: %s\n",
			strerror(errno));
		return 126;
	}
	if (write(fd, ctx, strlen(ctx)) < 0) {
		fprintf(stderr, "runcon: failed to transition to '%s': %s\n",
			ctx, strerror(errno));
		close(fd);
		return 126;
	}
	close(fd);

	execvp(argv[i], &argv[i]);
	if (argv[i][0] != '/') {
		char binpath[128];
		sprintf(binpath, "/bin/%s", argv[i]);
		execv(binpath, &argv[i]);
		sprintf(binpath, "/vendor/bin/%s", argv[i]);
		execv(binpath, &argv[i]);
	}
	fprintf(stderr, "runcon: failed to exec '%s': %s\n", argv[i], strerror(errno));
	return 127;
}

/* -------------------------------------------------------------------
 * load_policy
 * ------------------------------------------------------------------- */
static int load_one_policy_str(const char *policy_text, const char *label)
{
	int fd, rc;
	char errbuf[256];

	fd = open("/sys/fs/selinux/load", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "load_policy: cannot open /sys/fs/selinux/load: %s\n",
			strerror(errno));
		return 1;
	}
	rc = write(fd, policy_text, strlen(policy_text));
	if (rc < 0) {
		int saved_err = errno;
		errbuf[0] = '\0';
		lseek(fd, 0, SEEK_SET);
		if (read(fd, errbuf, sizeof(errbuf) - 1) > 0)
			errbuf[sizeof(errbuf) - 1] = '\0';
		close(fd);
		fprintf(stderr, "libsepol.report_failure: policy load failed for %s: %s (%s)\n",
			label,
			errbuf[0] ? errbuf : "neverallow / tier violation",
			strerror(saved_err));
		return 1;
	}
	close(fd);
	return 0;
}

static int load_policy_file(const char *path)
{
	static char buf[7680];
	int off = 0;
	int n;

	if (strstr(path, "/vendor/") != NULL) {
		strcpy(buf, "@tier vendor\n");
		off = strlen(buf);
	} else if (strstr(path, "/public/") != NULL) {
		strcpy(buf, "@tier public\n");
		off = strlen(buf);
	} else if (strstr(path, "/private/") != NULL) {
		strcpy(buf, "@tier private\n");
		off = strlen(buf);
	}
	n = read_text_file(path, buf + off, sizeof(buf) - off);
	if (n < 0) {
		fprintf(stderr, "load_policy: cannot read '%s': %s\n", path, strerror(errno));
		return 1;
	}
	return load_one_policy_str(buf, path);
}

static int load_policy_dir(const char *dirpath)
{
	DIR *d = opendir(dirpath);
	struct dirent *de;
	if (!d)
		return 0;
	while ((de = readdir(d)) != NULL) {
		int len = strlen(de->d_name);
		char full[256];
		if (len > 3 && !strcmp(de->d_name + len - 3, ".te")) {
			sprintf(full, "%s/%s", dirpath, de->d_name);
			if (load_policy_file(full) != 0) {
				closedir(d);
				return 1;
			}
		}
	}
	closedir(d);
	return 0;
}

static int do_load_policy(int argc, char **argv)
{
	int i = 1;

	if (argc >= 2 && !strcmp(argv[1], "-r")) {
		if (load_one_policy_str("@reset\n", "@reset") != 0)
			return 1;
		printf("SELinux: Baseline 3-tier policy restored.\n");
		return 0;
	}

	if (argc >= 3 && !strcmp(argv[1], "-e")) {
		if (load_one_policy_str(argv[2], "inline") != 0)
			return 1;
		printf("SELinux: Policy loaded and verified against all neverallow assertions.\n");
		return 0;
	}

	if (argc < 2) {
		/* Default: load 3-tier policy hierarchy */
		if (load_policy_dir("/system/etc/selinux/public") != 0)
			return 1;
		if (load_policy_dir("/system/etc/selinux/private") != 0)
			return 1;
		if (load_policy_dir("/vendor/etc/selinux") != 0)
			return 1;
		printf("SELinux: Loaded 3-tier policy (system/public + system/private + vendor).\n");
		return 0;
	}

	for (i = 1; i < argc; i++) {
		struct stat st;
		if (stat(argv[i], &st) == 0 && S_ISDIR(st.st_mode)) {
			if (load_policy_dir(argv[i]) != 0)
				return 1;
		} else {
			if (load_policy_file(argv[i]) != 0)
				return 1;
		}
	}
	printf("SELinux: Policy loaded and verified against all neverallow assertions.\n");
	return 0;
}

/* -------------------------------------------------------------------
 * audit2allow
 * ------------------------------------------------------------------- */
static void extract_field(const char *line, const char *key, char *out, int maxlen)
{
	const char *p = strstr(line, key);
	int i = 0;
	out[0] = '\0';
	if (!p)
		return;
	p += strlen(key);
	while (*p && *p != ' ' && *p != '\n' && *p != '\r' && i < maxlen - 1)
		out[i++] = *p++;
	out[i] = '\0';
}

static void ctx_to_type(const char *ctx, char *out, int maxlen)
{
	const char *p1, *p2, *p3;
	int len;
	out[0] = '\0';
	p1 = strchr(ctx, ':');
	if (!p1) {
		strncpy(out, ctx, maxlen - 1);
		out[maxlen - 1] = '\0';
		return;
	}
	p2 = strchr(p1 + 1, ':');
	if (!p2)
		return;
	p2++;
	p3 = strchr(p2, ':');
	len = p3 ? (int)(p3 - p2) : (int)strlen(p2);
	if (len >= maxlen)
		len = maxlen - 1;
	memcpy(out, p2, len);
	out[len] = '\0';
}

static int do_audit2allow(int argc, char **argv)
{
	static char buf[4096];
	static char seen[32][128];
	int nr_seen = 0;
	const char *in_path = "/sys/fs/selinux/avc";
	char *line, *next;
	char last_stype[64];
	int count = 0;

	if (argc >= 2 && !strcmp(argv[1], "-c")) {
		int fd = open("/sys/fs/selinux/avc", O_WRONLY);
		if (fd >= 0) {
			write(fd, "clear", 5);
			close(fd);
		}
		return 0;
	}
	if (argc >= 3 && !strcmp(argv[1], "-i"))
		in_path = argv[2];

	if (read_text_file(in_path, buf, sizeof(buf)) < 0) {
		fprintf(stderr, "audit2allow: cannot read '%s'\n", in_path);
		return 1;
	}

	last_stype[0] = '\0';
	line = buf;
	while (line && *line) {
		const char *b1, *b2;
		char perm[64], sctx[64], tctx[64], tclass[32], ioctlcmd[32];
		char stype[32], ttype[32], rule_str[128];
		int plen, j, dup = 0;

		next = strchr(line, '\n');
		if (next)
			*next++ = '\0';

		if (!strstr(line, "avc:  denied")) {
			line = next;
			continue;
		}
		b1 = strchr(line, '{');
		b2 = b1 ? strchr(b1, '}') : NULL;
		if (!b1 || !b2) {
			line = next;
			continue;
		}
		b1++;
		while (*b1 == ' ')
			b1++;
		plen = (int)(b2 - b1);
		while (plen > 0 && b1[plen - 1] == ' ')
			plen--;
		if (plen >= (int)sizeof(perm))
			plen = sizeof(perm) - 1;
		memcpy(perm, b1, plen);
		perm[plen] = '\0';

		extract_field(line, "scontext=", sctx, sizeof(sctx));
		extract_field(line, "tcontext=", tctx, sizeof(tctx));
		extract_field(line, "tclass=", tclass, sizeof(tclass));
		extract_field(line, "ioctlcmd=", ioctlcmd, sizeof(ioctlcmd));

		ctx_to_type(sctx, stype, sizeof(stype));
		ctx_to_type(tctx, ttype, sizeof(ttype));

		if (stype[0] && ttype[0] && tclass[0]) {
			if (!strcmp(stype, ttype) && !strcmp(tclass, "capability"))
				sprintf(rule_str, "allow %s self:%s %s;", stype, tclass, perm);
			else
				sprintf(rule_str, "allow %s %s:%s %s;", stype, ttype, tclass, perm);

			for (j = 0; j < nr_seen; j++) {
				if (!strcmp(seen[j], rule_str) && !ioctlcmd[0]) {
					dup = 1;
					break;
				}
			}
			if (!dup) {
				if (nr_seen < 32)
					strcpy(seen[nr_seen++], rule_str);
				if (strcmp(last_stype, stype) != 0) {
					printf("\n#============= %s ==============\n", stype);
					strcpy(last_stype, stype);
				}
				printf("%s\n", rule_str);
				if (ioctlcmd[0]) {
					printf("allowxperm %s %s:%s ioctl %s;\n",
					       stype, ttype, tclass, ioctlcmd);
				}
				count++;
			}
		}
		line = next;
	}
	if (count == 0)
		printf("# No AVC denials recorded in %s\n", in_path);
	return 0;
}

/* -------------------------------------------------------------------
 * seinfo / sestatus
 * ------------------------------------------------------------------- */
static int do_seinfo(int argc, char **argv)
{
	static char buf[4096];
	(void)argc;
	(void)argv;
	if (read_text_file("/proc/selinux", buf, sizeof(buf)) > 0)
		printf("%s\n", buf);
	if (read_text_file("/sys/fs/selinux/policy", buf, sizeof(buf)) > 0)
		printf("%s", buf);
	return 0;
}

static int do_sestatus(int argc, char **argv)
{
	static char buf[2048];
	(void)argc;
	(void)argv;
	if (read_text_file("/proc/selinux", buf, sizeof(buf)) > 0)
		printf("%s", buf);
	else
		printf("SELinux status:                 disabled\n");
	return 0;
}

int main(int argc, char **argv)
{
	const char *prog = prog_basename(argv[0]);

	if (!strcmp(prog, "selinux") && argc >= 2) {
		prog = argv[1];
		argc--;
		argv++;
	}

	if (!strcmp(prog, "getenforce"))
		return do_getenforce(argc, argv);
	if (!strcmp(prog, "setenforce"))
		return do_setenforce(argc, argv);
	if (!strcmp(prog, "chcon"))
		return do_chcon(argc, argv);
	if (!strcmp(prog, "restorecon"))
		return do_restorecon(argc, argv);
	if (!strcmp(prog, "runcon"))
		return do_runcon(argc, argv);
	if (!strcmp(prog, "load_policy"))
		return do_load_policy(argc, argv);
	if (!strcmp(prog, "audit2allow"))
		return do_audit2allow(argc, argv);
	if (!strcmp(prog, "seinfo"))
		return do_seinfo(argc, argv);
	if (!strcmp(prog, "sestatus"))
		return do_sestatus(argc, argv);

	fprintf(stderr,
		"Usage: %s <getenforce|setenforce|chcon|restorecon|runcon|load_policy|audit2allow|seinfo|sestatus>\n",
		prog);
	return 1;
}
