#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/time.h>
#include <pwd.h>
#include <stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char *crypt(const char *pw, const char *salt);
extern char *getpass(const char *prompt);
extern uid_t getuid(void);
extern int getpid(void);
extern int chmod(const char *path, mode_t mode);
extern int rename(const char *oldpath, const char *newpath);
extern int unlink(const char *pathname);

#define PASSWD_FILE "/etc/passwd"
#define PASSWD_TMP  "/etc/passwd.tmp"
#define SHADOW_FILE "/etc/shadow"
#define SHADOW_TMP  "/etc/shadow.tmp"

static const char salt_chars[] =
	"./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

static int
lookup_shadow_hash(const char *username, char *out, size_t outsz)
{
	FILE *fp;
	char line[256];
	size_t ulen = strlen(username);

	fp = fopen(SHADOW_FILE, "r");
	if (!fp)
		return -1;
	while (fgets(line, sizeof(line), fp)) {
		char *p, *end;
		if (strncmp(line, username, ulen) != 0 || line[ulen] != ':')
			continue;
		p = line + ulen + 1;
		end = strchr(p, ':');
		if (!end)
			end = strchr(p, '\n');
		if (end)
			*end = '\0';
		strncpy(out, p, outsz - 1);
		out[outsz - 1] = '\0';
		fclose(fp);
		return 0;
	}
	fclose(fp);
	return -1;
}

static int
update_passwd_file(const char *username, const char *new_hash)
{
	FILE *in, *out;
	char line[512];
	size_t ulen = strlen(username);
	int found = 0;

	in = fopen(PASSWD_FILE, "r");
	if (!in) {
		perror(PASSWD_FILE);
		return -1;
	}
	out = fopen(PASSWD_TMP, "w");
	if (!out) {
		perror(PASSWD_TMP);
		fclose(in);
		return -1;
	}

	while (fgets(line, sizeof(line), in)) {
		if (!found && strncmp(line, username, ulen) == 0 && line[ulen] == ':') {
			char *rest = strchr(line + ulen + 1, ':');
			if (rest) {
				fprintf(out, "%s:%s%s", username, new_hash, rest);
				found = 1;
				continue;
			}
		}
		fputs(line, out);
	}

	fclose(in);
	fclose(out);

	if (!found) {
		unlink(PASSWD_TMP);
		return -1;
	}
	chmod(PASSWD_TMP, 0644);
	if (rename(PASSWD_TMP, PASSWD_FILE) < 0) {
		perror("rename " PASSWD_FILE);
		unlink(PASSWD_TMP);
		return -1;
	}
	return 0;
}

static void
update_shadow_file(const char *username, const char *new_hash)
{
	FILE *in, *out;
	char line[512];
	size_t ulen = strlen(username);
	int found = 0;

	in = fopen(SHADOW_FILE, "r");
	if (!in)
		return;
	out = fopen(SHADOW_TMP, "w");
	if (!out) {
		fclose(in);
		return;
	}

	while (fgets(line, sizeof(line), in)) {
		if (!found && strncmp(line, username, ulen) == 0 && line[ulen] == ':') {
			char *rest = strchr(line + ulen + 1, ':');
			if (rest) {
				fprintf(out, "%s:%s%s", username, new_hash, rest);
				found = 1;
				continue;
			}
		}
		fputs(line, out);
	}

	fclose(in);
	fclose(out);

	if (!found) {
		unlink(SHADOW_TMP);
		return;
	}
	chmod(SHADOW_TMP, 0600);
	if (rename(SHADOW_TMP, SHADOW_FILE) < 0)
		unlink(SHADOW_TMP);
}

static int
is_locked_hash(const char *h)
{
	if (!h || !h[0])
		return 0;
	if (h[0] == '!' || h[0] == '*' || strcmp(h, "NP") == 0)
		return 1;
	return 0;
}

static void
usage(void)
{
	fprintf(stderr, "Usage: passwd [-d | -l | -u | -S | --stdin] [username]\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	int opt_delete = 0, opt_lock = 0, opt_unlock = 0, opt_status = 0, opt_stdin = 0;
	const char *target_user = NULL;
	struct passwd *pw;
	uid_t my_uid;
	char cur_hash[128];
	char new_hash[128];
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-d") == 0)
			opt_delete = 1;
		else if (strcmp(argv[i], "-l") == 0)
			opt_lock = 1;
		else if (strcmp(argv[i], "-u") == 0)
			opt_unlock = 1;
		else if (strcmp(argv[i], "-S") == 0 || strcmp(argv[i], "--status") == 0)
			opt_status = 1;
		else if (strcmp(argv[i], "--stdin") == 0)
			opt_stdin = 1;
		else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
			usage();
		else if (argv[i][0] == '-')
			usage();
		else if (!target_user)
			target_user = argv[i];
		else
			usage();
	}

	my_uid = getuid();
	if (!target_user) {
		pw = getpwuid(my_uid);
		if (!pw) {
			fprintf(stderr, "passwd: cannot determine user name for uid %d\n", (int)my_uid);
			exit(1);
		}
		target_user = pw->pw_name;
	} else {
		pw = getpwnam(target_user);
		if (!pw) {
			fprintf(stderr, "passwd: user '%s' does not exist\n", target_user);
			exit(1);
		}
	}

	if (my_uid != 0 && pw->pw_uid != my_uid) {
		fprintf(stderr, "passwd: permission denied\n");
		exit(1);
	}

	if (my_uid != 0 && (opt_delete || opt_lock || opt_unlock)) {
		fprintf(stderr, "passwd: only root may use -d, -l, or -u\n");
		exit(1);
	}

	strncpy(cur_hash, pw->pw_passwd ? pw->pw_passwd : "", sizeof(cur_hash) - 1);
	cur_hash[sizeof(cur_hash) - 1] = '\0';
	if (strcmp(cur_hash, "x") == 0)
		lookup_shadow_hash(pw->pw_name, cur_hash, sizeof(cur_hash));

	if (opt_status) {
		const char *st = "P";
		if (cur_hash[0] == '\0')
			st = "NP";
		else if (is_locked_hash(cur_hash))
			st = "L";
		printf("%s %s\n", pw->pw_name, st);
		return 0;
	}

	if (opt_delete) {
		if (update_passwd_file(pw->pw_name, "") < 0)
			exit(1);
		update_shadow_file(pw->pw_name, "");
		printf("passwd: password expiry information changed (password deleted for %s).\n", pw->pw_name);
		return 0;
	}

	if (opt_lock) {
		if (cur_hash[0] == '!') {
			strncpy(new_hash, cur_hash, sizeof(new_hash) - 1);
		} else if (cur_hash[0] == '\0' || strcmp(cur_hash, "NP") == 0) {
			strcpy(new_hash, "!NP");
		} else {
			new_hash[0] = '!';
			strncpy(new_hash + 1, cur_hash, sizeof(new_hash) - 2);
			new_hash[sizeof(new_hash) - 1] = '\0';
		}
		if (update_passwd_file(pw->pw_name, new_hash) < 0)
			exit(1);
		update_shadow_file(pw->pw_name, new_hash);
		printf("passwd: password locked for %s.\n", pw->pw_name);
		return 0;
	}

	if (opt_unlock) {
		if (strcmp(cur_hash, "!NP") == 0 || strcmp(cur_hash, "NP") == 0 || strcmp(cur_hash, "*") == 0) {
			new_hash[0] = '\0';
		} else if (cur_hash[0] == '!') {
			strncpy(new_hash, cur_hash + 1, sizeof(new_hash) - 1);
			new_hash[sizeof(new_hash) - 1] = '\0';
		} else {
			strncpy(new_hash, cur_hash, sizeof(new_hash) - 1);
			new_hash[sizeof(new_hash) - 1] = '\0';
		}
		if (update_passwd_file(pw->pw_name, new_hash) < 0)
			exit(1);
		update_shadow_file(pw->pw_name, new_hash);
		printf("passwd: password unlocked for %s.\n", pw->pw_name);
		return 0;
	}

	if (opt_stdin) {
		char plain[64];
		char salt[3];
		unsigned long seed;
		size_t len;

		if (!fgets(plain, sizeof(plain), stdin)) {
			fprintf(stderr, "passwd: error reading password from stdin\n");
			exit(1);
		}
		len = strlen(plain);
		while (len > 0 && (plain[len - 1] == '\n' || plain[len - 1] == '\r'))
			plain[--len] = '\0';

		seed = (unsigned long)time(NULL) ^ ((unsigned long)getpid() << 8);
		salt[0] = salt_chars[seed & 63];
		salt[1] = salt_chars[(seed >> 6) & 63];
		salt[2] = '\0';
		strncpy(new_hash, crypt(plain, salt), sizeof(new_hash) - 1);
		new_hash[sizeof(new_hash) - 1] = '\0';
	} else {
		char pw1[64], pw2[64];
		char salt[3];
		char *p;
		unsigned long seed;

		printf("Changing password for %s\n", pw->pw_name);
		if (my_uid != 0 && cur_hash[0] != '\0') {
			if (is_locked_hash(cur_hash)) {
				fprintf(stderr, "passwd: password for %s is locked\n", pw->pw_name);
				exit(1);
			}
			p = getpass("Old password: ");
			if (!p)
				exit(1);
			salt[0] = cur_hash[0];
			salt[1] = cur_hash[1];
			salt[2] = '\0';
			if (strcmp(crypt(p, salt), cur_hash) != 0) {
				fprintf(stderr, "passwd: authentication failure\n");
				exit(1);
			}
		}

		p = getpass("New password: ");
		if (!p)
			exit(1);
		strncpy(pw1, p, sizeof(pw1) - 1);
		pw1[sizeof(pw1) - 1] = '\0';

		p = getpass("Retype new password: ");
		if (!p)
			exit(1);
		strncpy(pw2, p, sizeof(pw2) - 1);
		pw2[sizeof(pw2) - 1] = '\0';

		if (strcmp(pw1, pw2) != 0) {
			fprintf(stderr, "passwd: passwords do not match\n");
			exit(1);
		}

		if (pw1[0] == '\0') {
			new_hash[0] = '\0';
		} else {
			seed = (unsigned long)time(NULL) ^ ((unsigned long)getpid() << 8);
			salt[0] = salt_chars[seed & 63];
			salt[1] = salt_chars[(seed >> 6) & 63];
			salt[2] = '\0';
			strncpy(new_hash, crypt(pw1, salt), sizeof(new_hash) - 1);
			new_hash[sizeof(new_hash) - 1] = '\0';
		}
	}

	if (update_passwd_file(pw->pw_name, new_hash) < 0)
		exit(1);
	update_shadow_file(pw->pw_name, new_hash);
	printf("passwd: password updated successfully\n");
	return 0;
}
