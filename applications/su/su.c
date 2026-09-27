#include <linux/types.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char **envlist;
extern char *crypt(const char *pw, const char *salt);
extern char *getpass(const char *prompt);
extern uid_t getuid(void);
extern int setuid(uid_t uid);
extern int setgid(gid_t gid);
extern int chdir(const char *path);
extern int execve(const char *filename, char *const argv[], char *const envp[]);

#define SHADOW_FILE "/etc/shadow"

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

static void
set_env_var(char **env, const char *entry)
{
	const char *eq = strchr(entry, '=');
	size_t keylen;
	if (!eq)
		return;
	keylen = (size_t)(eq - entry + 1);
	for (; *env; env++) {
		if (strncmp(*env, entry, keylen) == 0) {
			*env = (char *)entry;
			return;
		}
	}
	*env = (char *)entry;
	env[1] = NULL;
}

static void
usage(void)
{
	fprintf(stderr, "Usage: su [- | -l] [-c command] [-s shell] [username]\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	int login_shell = 0;
	const char *cmd = NULL;
	const char *override_shell = NULL;
	const char *target_user = "root";
	struct passwd *pw;
	uid_t caller_uid;
	char cur_hash[128];
	char sh_buf[128];
	char arg0[64];
	char env_user[64], env_logname[64], env_home[160], env_shell[160], env_path[128];
	char *new_env[64];
	char *sh_argv[4];
	int i, env_cnt = 0;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-") == 0 || strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--login") == 0) {
			login_shell = 1;
		} else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
			cmd = argv[++i];
		} else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
			override_shell = argv[++i];
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			usage();
		} else if (argv[i][0] == '-') {
			usage();
		} else {
			target_user = argv[i];
		}
	}

	pw = getpwnam(target_user);
	if (!pw) {
		fprintf(stderr, "su: user %s does not exist\n", target_user);
		exit(1);
	}

	caller_uid = getuid();
	strncpy(cur_hash, pw->pw_passwd ? pw->pw_passwd : "", sizeof(cur_hash) - 1);
	cur_hash[sizeof(cur_hash) - 1] = '\0';
	if (strcmp(cur_hash, "x") == 0)
		lookup_shadow_hash(pw->pw_name, cur_hash, sizeof(cur_hash));

	if (caller_uid != 0 && cur_hash[0] != '\0') {
		char *pass;
		char salt[3];
		if (cur_hash[0] == '!' || cur_hash[0] == '*' || strcmp(cur_hash, "NP") == 0) {
			fprintf(stderr, "su: Authentication failure\n");
			exit(1);
		}
		pass = getpass("Password: ");
		if (!pass) {
			fprintf(stderr, "su: Authentication failure\n");
			exit(1);
		}
		salt[0] = cur_hash[0];
		salt[1] = cur_hash[1];
		salt[2] = '\0';
		if (strcmp(crypt(pass, salt), cur_hash) != 0) {
			fprintf(stderr, "su: Authentication failure\n");
			exit(1);
		}
	}

	if (setgid(pw->pw_gid) < 0) {
		perror("su: setgid");
		exit(1);
	}
	if (setuid(pw->pw_uid) < 0) {
		perror("su: setuid");
		exit(1);
	}

	if (override_shell && override_shell[0])
		strncpy(sh_buf, override_shell, sizeof(sh_buf) - 1);
	else if (pw->pw_shell && pw->pw_shell[0])
		strncpy(sh_buf, pw->pw_shell, sizeof(sh_buf) - 1);
	else
		strcpy(sh_buf, "/bin/sh");
	sh_buf[sizeof(sh_buf) - 1] = '\0';

	if (!login_shell && envlist) {
		for (i = 0; envlist[i] && env_cnt < 56; i++)
			new_env[env_cnt++] = envlist[i];
	}
	new_env[env_cnt] = NULL;

	snprintf(env_user, sizeof(env_user), "USER=%s", pw->pw_name);
	snprintf(env_logname, sizeof(env_logname), "LOGNAME=%s", pw->pw_name);
	snprintf(env_home, sizeof(env_home), "HOME=%s", (pw->pw_dir && pw->pw_dir[0]) ? pw->pw_dir : "/");
	snprintf(env_shell, sizeof(env_shell), "SHELL=%s", sh_buf);
	strcpy(env_path, "PATH=/bin:/usr/bin:/etc");

	set_env_var(new_env, env_user);
	set_env_var(new_env, env_logname);
	set_env_var(new_env, env_home);
	set_env_var(new_env, env_shell);
	set_env_var(new_env, env_path);
	if (login_shell)
		set_env_var(new_env, "TERM=linux");

	if (login_shell && pw->pw_dir && pw->pw_dir[0])
		chdir(pw->pw_dir);

	{
		const char *base = strrchr(sh_buf, '/');
		base = base ? base + 1 : sh_buf;
		if (login_shell && !cmd)
			snprintf(arg0, sizeof(arg0), "-%s", base);
		else
			snprintf(arg0, sizeof(arg0), "%s", base);
	}

	sh_argv[0] = arg0;
	if (cmd) {
		sh_argv[1] = "-c";
		sh_argv[2] = (char *)cmd;
		sh_argv[3] = NULL;
	} else {
		sh_argv[1] = NULL;
	}

	execve(sh_buf, sh_argv, new_env);
	if (strcmp(sh_buf, "/bin/sh") != 0) {
		strcpy(sh_buf, "/bin/sh");
		sh_argv[0] = login_shell ? "-sh" : "sh";
		execve(sh_buf, sh_argv, new_env);
	}
	perror("su: execve");
	exit(1);
}
