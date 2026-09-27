#include <linux/types.h>
#include <pwd.h>
#include <grp.h>
#include <stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uid_t getuid(void);
extern int chmod(const char *path, mode_t mode);
extern int chown(const char *path, uid_t owner, gid_t group);
extern int rename(const char *oldpath, const char *newpath);
extern int unlink(const char *pathname);

#define PASSWD_FILE "/etc/passwd"
#define PASSWD_TMP  "/etc/passwd.tmp"
#define SHADOW_FILE "/etc/shadow"
#define SHADOW_TMP  "/etc/shadow.tmp"

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

static void
usage(void)
{
	fprintf(stderr,
		"Usage: usermod [-u uid] [-g group] [-d home] [-m] [-s shell] [-c comment] [-l new_name] [-L|-U] username\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	int set_uid = 0, set_gid = 0, move_home = 0, opt_lock = 0, opt_unlock = 0;
	uid_t new_uid = 0;
	gid_t new_gid = 0;
	const char *new_home = NULL;
	const char *new_shell = NULL;
	const char *new_comment = NULL;
	const char *new_name = NULL;
	const char *new_pass = NULL;
	const char *username = NULL;
	struct passwd *pw;
	char old_name[64], old_pass[128], old_gecos[128], old_dir[128], old_shell[128];
	uid_t cur_uid;
	gid_t cur_gid;
	char final_pass[128];
	FILE *in, *out;
	char line[512];
	size_t ulen;
	int i, updated = 0;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) {
			i++;
			if (!is_number(argv[i])) {
				fprintf(stderr, "usermod: invalid numeric uid '%s'\n", argv[i]);
				exit(1);
			}
			new_uid = (uid_t)atoi(argv[i]);
			set_uid = 1;
		} else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
			i++;
			if (is_number(argv[i])) {
				new_gid = (gid_t)atoi(argv[i]);
			} else {
				struct group *gr = getgrnam(argv[i]);
				if (!gr) {
					fprintf(stderr, "usermod: group '%s' does not exist\n", argv[i]);
					exit(1);
				}
				new_gid = gr->gr_gid;
			}
			set_gid = 1;
		} else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
			new_home = argv[++i];
		} else if (strcmp(argv[i], "-m") == 0) {
			move_home = 1;
		} else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
			new_shell = argv[++i];
		} else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
			new_comment = argv[++i];
		} else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
			new_name = argv[++i];
		} else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
			new_pass = argv[++i];
		} else if (strcmp(argv[i], "-L") == 0) {
			opt_lock = 1;
		} else if (strcmp(argv[i], "-U") == 0) {
			opt_unlock = 1;
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			usage();
		} else if (argv[i][0] == '-') {
			usage();
		} else if (!username) {
			username = argv[i];
		} else {
			usage();
		}
	}

	if (!username)
		usage();

	if (getuid() != 0) {
		fprintf(stderr, "usermod: permission denied (must be root)\n");
		exit(1);
	}

	pw = getpwnam(username);
	if (!pw) {
		fprintf(stderr, "usermod: user '%s' does not exist\n", username);
		exit(1);
	}

	strncpy(old_name, pw->pw_name, sizeof(old_name) - 1);
	old_name[sizeof(old_name) - 1] = '\0';
	strncpy(old_pass, pw->pw_passwd ? pw->pw_passwd : "", sizeof(old_pass) - 1);
	old_pass[sizeof(old_pass) - 1] = '\0';
	cur_uid = pw->pw_uid;
	cur_gid = pw->pw_gid;
	strncpy(old_gecos, pw->pw_gecos ? pw->pw_gecos : "", sizeof(old_gecos) - 1);
	old_gecos[sizeof(old_gecos) - 1] = '\0';
	strncpy(old_dir, pw->pw_dir ? pw->pw_dir : "/", sizeof(old_dir) - 1);
	old_dir[sizeof(old_dir) - 1] = '\0';
	strncpy(old_shell, pw->pw_shell ? pw->pw_shell : "/bin/sh", sizeof(old_shell) - 1);
	old_shell[sizeof(old_shell) - 1] = '\0';

	if (new_name && strcmp(new_name, old_name) != 0) {
		if (getpwnam(new_name) != NULL) {
			fprintf(stderr, "usermod: user '%s' already exists\n", new_name);
			exit(1);
		}
	}

	if (set_uid && new_uid != cur_uid) {
		if (getpwuid(new_uid) != NULL) {
			fprintf(stderr, "usermod: UID %d already exists\n", (int)new_uid);
			exit(1);
		}
		cur_uid = new_uid;
	}

	if (set_gid)
		cur_gid = new_gid;

	if (new_pass) {
		strncpy(final_pass, new_pass, sizeof(final_pass) - 1);
		final_pass[sizeof(final_pass) - 1] = '\0';
	} else if (opt_lock) {
		if (old_pass[0] == '!') {
			strncpy(final_pass, old_pass, sizeof(final_pass) - 1);
		} else if (old_pass[0] == '\0' || strcmp(old_pass, "NP") == 0) {
			strcpy(final_pass, "!NP");
		} else {
			final_pass[0] = '!';
			strncpy(final_pass + 1, old_pass, sizeof(final_pass) - 2);
			final_pass[sizeof(final_pass) - 1] = '\0';
		}
	} else if (opt_unlock) {
		if (strcmp(old_pass, "!NP") == 0 || strcmp(old_pass, "NP") == 0)
			final_pass[0] = '\0';
		else if (old_pass[0] == '!')
			strncpy(final_pass, old_pass + 1, sizeof(final_pass) - 1);
		else
			strncpy(final_pass, old_pass, sizeof(final_pass) - 1);
		final_pass[sizeof(final_pass) - 1] = '\0';
	} else {
		strncpy(final_pass, old_pass, sizeof(final_pass) - 1);
		final_pass[sizeof(final_pass) - 1] = '\0';
	}

	in = fopen(PASSWD_FILE, "r");
	if (!in) {
		perror(PASSWD_FILE);
		exit(1);
	}
	out = fopen(PASSWD_TMP, "w");
	if (!out) {
		perror(PASSWD_TMP);
		fclose(in);
		exit(1);
	}

	ulen = strlen(old_name);
	while (fgets(line, sizeof(line), in)) {
		if (!updated && strncmp(line, old_name, ulen) == 0 && line[ulen] == ':') {
			fprintf(out, "%s:%s:%d:%d:%s:%s:%s\n",
				new_name ? new_name : old_name,
				final_pass,
				(int)cur_uid,
				(int)cur_gid,
				new_comment ? new_comment : old_gecos,
				new_home ? new_home : old_dir,
				new_shell ? new_shell : old_shell);
			updated = 1;
			continue;
		}
		fputs(line, out);
	}

	fclose(in);
	fclose(out);

	if (!updated) {
		unlink(PASSWD_TMP);
		exit(1);
	}
	chmod(PASSWD_TMP, 0644);
	if (rename(PASSWD_TMP, PASSWD_FILE) < 0) {
		perror("rename " PASSWD_FILE);
		unlink(PASSWD_TMP);
		exit(1);
	}

	if (new_name || new_pass || opt_lock || opt_unlock) {
		in = fopen(SHADOW_FILE, "r");
		if (in) {
			out = fopen(SHADOW_TMP, "w");
			if (out) {
				int s_updated = 0;
				while (fgets(line, sizeof(line), in)) {
					if (!s_updated && strncmp(line, old_name, ulen) == 0 && line[ulen] == ':') {
						char *rest = strchr(line + ulen + 1, ':');
						if (rest) {
							fprintf(out, "%s:%s%s",
								new_name ? new_name : old_name,
								final_pass,
								rest);
							s_updated = 1;
							continue;
						}
					}
					fputs(line, out);
				}
				fclose(out);
				if (s_updated) {
					chmod(SHADOW_TMP, 0600);
					rename(SHADOW_TMP, SHADOW_FILE);
				} else {
					unlink(SHADOW_TMP);
				}
			}
			fclose(in);
		}
	}

	if (move_home && new_home && strcmp(new_home, old_dir) != 0) {
		struct stat st;
		if (stat(old_dir, &st) == 0) {
			if (rename(old_dir, new_home) < 0) {
				perror("usermod: moving home directory");
				exit(1);
			}
			chown(new_home, cur_uid, cur_gid);
		}
	}

	return 0;
}
