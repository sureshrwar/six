#include <linux/types.h>
#include <linux/dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int chown(const char *path, uid_t owner, gid_t group);
extern int getdents(unsigned int fd, struct dirent *dirp, unsigned int count);

static int
is_number(const char *s)
{
	if (!s || !s[0])
		return 0;
	for (; *s; s++) {
		if (*s < '0' || *s > '9')
			return 0;
	}
	return 1;
}

static int
parse_owner_group(const char *spec, int *set_uid, uid_t *uid_out, int *set_gid, gid_t *gid_out)
{
	char buf[128];
	char *sep;

	*set_uid = 0;
	*set_gid = 0;
	strncpy(buf, spec, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	sep = strchr(buf, ':');
	if (!sep)
		sep = strchr(buf, '.');
	if (sep)
		*sep++ = '\0';

	if (buf[0] != '\0') {
		if (is_number(buf)) {
			*uid_out = (uid_t)atoi(buf);
		} else {
			struct passwd *pw = getpwnam(buf);
			if (!pw) {
				fprintf(stderr, "chown: invalid user: '%s'\n", buf);
				return -1;
			}
			*uid_out = pw->pw_uid;
			if (sep && sep[0] == '\0') {
				*gid_out = pw->pw_gid;
				*set_gid = 1;
			}
		}
		*set_uid = 1;
	}

	if (sep && sep[0] != '\0') {
		if (is_number(sep)) {
			*gid_out = (gid_t)atoi(sep);
		} else {
			struct group *gr = getgrnam(sep);
			if (!gr) {
				fprintf(stderr, "chown: invalid group: '%s'\n", sep);
				return -1;
			}
			*gid_out = gr->gr_gid;
		}
		*set_gid = 1;
	}

	if (!*set_uid && !*set_gid)
		return -1;
	return 0;
}

static int
apply_chown(const char *path, int recursive, int set_uid, uid_t uid, int set_gid, gid_t gid)
{
	struct stat st;
	uid_t target_uid;
	gid_t target_gid;
	int rc = 0;

	if (lstat(path, &st) < 0) {
		perror(path);
		return 1;
	}

	target_uid = set_uid ? uid : st.st_uid;
	target_gid = set_gid ? gid : st.st_gid;

	if (chown(path, target_uid, target_gid) < 0) {
		perror(path);
		rc = 1;
	}

	if (recursive && S_ISDIR(st.st_mode)) {
		char dbuf[512];
		int fd = open(path, 0);
		if (fd >= 0) {
			int n;
			while ((n = getdents(fd, (struct dirent *)dbuf, sizeof(dbuf))) > 0) {
				int off = 0;
				while (off < n) {
					struct dirent *de = (struct dirent *)(dbuf + off);
					char child[256];
					off += de->d_reclen;
					if (de->d_ino == 0)
						continue;
					if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
						continue;
					if (strcmp(path, "/") == 0)
						snprintf(child, sizeof(child), "/%s", de->d_name);
					else
						snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
					if (apply_chown(child, 1, set_uid, uid, set_gid, gid) != 0)
						rc = 1;
				}
			}
			close(fd);
		}
	}
	return rc;
}

int
main(int argc, char **argv)
{
	int recursive = 0;
	int i = 1, rc = 0;
	int set_uid = 0, set_gid = 0;
	uid_t uid = 0;
	gid_t gid = 0;

	if (i < argc && strcmp(argv[i], "-R") == 0) {
		recursive = 1;
		i++;
	}

	if (argc - i < 2) {
		fprintf(stderr, "Usage: chown [-R] OWNER[:GROUP] FILE...\n");
		exit(1);
	}

	if (parse_owner_group(argv[i++], &set_uid, &uid, &set_gid, &gid) < 0)
		exit(1);

	for (; i < argc; i++) {
		if (apply_chown(argv[i], recursive, set_uid, uid, set_gid, gid) != 0)
			rc = 1;
	}
	exit(rc);
	return rc;
}
