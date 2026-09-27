#include <linux/types.h>
#include <linux/fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uid_t getuid(void);
extern int mkdir(const char *pathname, mode_t mode);
extern int chown(const char *path, uid_t owner, gid_t group);
extern int chmod(const char *path, mode_t mode);

#define PASSWD_FILE "/etc/passwd"
#define SHADOW_FILE "/etc/shadow"
#define GROUP_FILE  "/etc/group"

static int
valid_username(const char *name)
{
	const char *p = name;
	if (!name || !name[0] || strlen(name) > 31)
		return 0;
	if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_'))
		return 0;
	for (; *p; p++) {
		if (!((*p >= 'a' && *p <= 'z') ||
		      (*p >= 'A' && *p <= 'Z') ||
		      (*p >= '0' && *p <= '9') ||
		      *p == '_' || *p == '-' || *p == '.'))
			return 0;
	}
	return 1;
}

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

static uid_t
find_free_uid(void)
{
	uid_t candidate = 1000;
	for (; candidate < 60000; candidate++) {
		if (getpwuid(candidate) == NULL)
			return candidate;
	}
	return 1000;
}

static void
usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [-m] [-u uid] [-g group] [-d home] [-s shell] [-c comment] [-p passwd] username\n",
		prog);
	exit(1);
}

int
main(int argc, char **argv)
{
	const char *prog = argv[0];
	const char *slash = strrchr(prog, '/');
	int opt_create_home = 0;
	int uid_specified = 0;
	int gid_specified = 0;
	uid_t uid = 0;
	gid_t gid = 0;
	const char *home_dir = NULL;
	const char *shell = "/bin/sh";
	const char *comment = "";
	const char *passwd_hash = "";
	const char *username = NULL;
	char default_home[128];
	FILE *fp;
	int i;

	if (slash)
		prog = slash + 1;
	if (strcmp(prog, "adduser") == 0)
		opt_create_home = 1;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--create-home") == 0) {
			opt_create_home = 1;
		} else if (strcmp(argv[i], "-M") == 0) {
			opt_create_home = 0;
		} else if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) {
			i++;
			if (!is_number(argv[i])) {
				fprintf(stderr, "%s: invalid numeric uid '%s'\n", prog, argv[i]);
				exit(1);
			}
			uid = (uid_t)atoi(argv[i]);
			uid_specified = 1;
		} else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
			i++;
			if (is_number(argv[i])) {
				gid = (gid_t)atoi(argv[i]);
			} else {
				struct group *gr = getgrnam(argv[i]);
				if (!gr) {
					fprintf(stderr, "%s: group '%s' does not exist\n", prog, argv[i]);
					exit(1);
				}
				gid = gr->gr_gid;
			}
			gid_specified = 1;
		} else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
			home_dir = argv[++i];
		} else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
			shell = argv[++i];
		} else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
			comment = argv[++i];
		} else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
			passwd_hash = argv[++i];
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			usage(prog);
		} else if (argv[i][0] == '-') {
			usage(prog);
		} else if (!username) {
			username = argv[i];
		} else {
			usage(prog);
		}
	}

	if (!username)
		usage(prog);

	if (getuid() != 0) {
		fprintf(stderr, "%s: permission denied (must be root)\n", prog);
		exit(1);
	}

	if (!valid_username(username)) {
		fprintf(stderr, "%s: invalid user name '%s'\n", prog, username);
		exit(1);
	}

	if (getpwnam(username) != NULL) {
		fprintf(stderr, "%s: user '%s' already exists\n", prog, username);
		exit(1);
	}

	if (!uid_specified) {
		uid = find_free_uid();
	} else if (getpwuid(uid) != NULL) {
		fprintf(stderr, "%s: UID %d already exists\n", prog, (int)uid);
		exit(1);
	}

	if (!gid_specified) {
		struct group *gr = getgrnam(username);
		if (gr) {
			gid = gr->gr_gid;
		} else {
			gid = (gid_t)uid;
			if (getgrgid(gid) != NULL) {
				struct group *users_gr = getgrnam("users");
				gid = users_gr ? users_gr->gr_gid : 100;
			} else {
				fp = fopen(GROUP_FILE, "a");
				if (fp) {
					fprintf(fp, "%s::%d:\n", username, (int)gid);
					fclose(fp);
				}
			}
		}
	}

	if (!home_dir) {
		sprintf(default_home, "/home/%s", username);
		home_dir = default_home;
	}

	fp = fopen(PASSWD_FILE, "a");
	if (!fp) {
		perror(PASSWD_FILE);
		exit(1);
	}
	fprintf(fp, "%s:%s:%d:%d:%s:%s:%s\n",
		username, passwd_hash, (int)uid, (int)gid, comment, home_dir, shell);
	fclose(fp);

	fp = fopen(SHADOW_FILE, "a");
	if (fp) {
		fprintf(fp, "%s:%s:12000::::::\n", username, passwd_hash);
		fclose(fp);
	}

	if (opt_create_home) {
		struct stat st;
		char prof_path[256];

		if (stat("/home", &st) < 0) {
			mkdir("/home", 0755);
			chown("/home", 0, 0);
		}
		if (stat(home_dir, &st) < 0) {
			if (mkdir(home_dir, 0755) < 0) {
				perror(home_dir);
				exit(1);
			}
		}
		chown(home_dir, uid, gid);
		chmod(home_dir, 0755);

		if (strlen(home_dir) + 12 < sizeof(prof_path)) {
			sprintf(prof_path, "%s/.profile", home_dir);
			if (stat(prof_path, &st) < 0) {
				fp = fopen(prof_path, "w");
				if (fp) {
					fprintf(fp, "PATH=/bin:/usr/bin:/etc\nexport PATH\n");
					fclose(fp);
					chown(prof_path, uid, gid);
					chmod(prof_path, 0644);
				}
			}
		}
	}

	return 0;
}
