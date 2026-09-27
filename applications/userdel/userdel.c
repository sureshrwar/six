#include <linux/types.h>
#include <linux/dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern uid_t getuid(void);
extern int getdents(unsigned int fd, struct dirent *dirp, unsigned int count);
extern int chmod(const char *path, mode_t mode);
extern int rename(const char *oldpath, const char *newpath);
extern int unlink(const char *pathname);
extern int rmdir(const char *pathname);

#define PASSWD_FILE "/etc/passwd"
#define PASSWD_TMP  "/etc/passwd.tmp"
#define SHADOW_FILE "/etc/shadow"
#define SHADOW_TMP  "/etc/shadow.tmp"
#define GROUP_FILE  "/etc/group"
#define GROUP_TMP   "/etc/group.tmp"

static int
remove_tree(const char *path)
{
	struct stat st;
	if (lstat(path, &st) < 0)
		return -1;
	if (S_ISDIR(st.st_mode)) {
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
					snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
					remove_tree(child);
				}
			}
			close(fd);
		}
		return rmdir(path);
	}
	return unlink(path);
}

static void
remove_entry_from_file(const char *filepath, const char *tmppath, const char *username, mode_t mode)
{
	FILE *in, *out;
	char line[512];
	size_t ulen = strlen(username);
	int removed = 0;

	in = fopen(filepath, "r");
	if (!in)
		return;
	out = fopen(tmppath, "w");
	if (!out) {
		fclose(in);
		return;
	}
	while (fgets(line, sizeof(line), in)) {
		if (strncmp(line, username, ulen) == 0 && line[ulen] == ':') {
			removed = 1;
			continue;
		}
		fputs(line, out);
	}
	fclose(in);
	fclose(out);
	if (removed) {
		chmod(tmppath, mode);
		rename(tmppath, filepath);
	} else {
		unlink(tmppath);
	}
}

static void
usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [-r] username\n", prog);
	exit(1);
}

int
main(int argc, char **argv)
{
	const char *prog = argv[0];
	const char *slash = strrchr(prog, '/');
	int opt_remove_home = 0;
	const char *username = NULL;
	struct passwd *pw;
	char home_dir[128];
	int i;

	if (slash)
		prog = slash + 1;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--remove") == 0)
			opt_remove_home = 1;
		else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
			usage(prog);
		else if (argv[i][0] == '-')
			usage(prog);
		else if (!username)
			username = argv[i];
		else
			usage(prog);
	}

	if (!username)
		usage(prog);

	if (getuid() != 0) {
		fprintf(stderr, "%s: permission denied (must be root)\n", prog);
		exit(1);
	}

	pw = getpwnam(username);
	if (!pw) {
		fprintf(stderr, "%s: user '%s' does not exist\n", prog, username);
		exit(1);
	}

	if (pw->pw_uid == 0 || strcmp(pw->pw_name, "root") == 0) {
		fprintf(stderr, "%s: cannot remove root account\n", prog);
		exit(1);
	}

	strncpy(home_dir, pw->pw_dir ? pw->pw_dir : "", sizeof(home_dir) - 1);
	home_dir[sizeof(home_dir) - 1] = '\0';

	remove_entry_from_file(PASSWD_FILE, PASSWD_TMP, username, 0644);
	remove_entry_from_file(SHADOW_FILE, SHADOW_TMP, username, 0600);
	remove_entry_from_file(GROUP_FILE, GROUP_TMP, username, 0644);

	if (opt_remove_home && home_dir[0] != '\0' &&
	    strcmp(home_dir, "/") != 0 &&
	    strcmp(home_dir, "/bin") != 0 &&
	    strcmp(home_dir, "/etc") != 0 &&
	    strcmp(home_dir, "/usr") != 0 &&
	    strcmp(home_dir, "/var") != 0 &&
	    strcmp(home_dir, "/tmp") != 0 &&
	    strcmp(home_dir, "/home") != 0) {
		char mail_path[128];
		remove_tree(home_dir);
		snprintf(mail_path, sizeof(mail_path), "/var/spool/mail/%s", username);
		unlink(mail_path);
		snprintf(mail_path, sizeof(mail_path), "/var/mail/%s", username);
		unlink(mail_path);
	}

	return 0;
}
