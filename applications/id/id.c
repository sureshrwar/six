#include <linux/types.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static int get_self_context(char *buf, int maxlen)
{
	int fd, n;
	buf[0] = '\0';
	fd = open("/proc/self/attr/current", O_RDONLY);
	if (fd < 0)
		return -1;
	n = read(fd, buf, maxlen - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = '\0';
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
		buf[--n] = '\0';
	return 0;
}

int main(int argc, char **argv)
{
	struct passwd *pwd;
	struct group *grp;
	int uid, gid, euid, egid;
	char sectx[64];

	if (argc >= 2 && strcmp(argv[1], "-Z") == 0) {
		if (get_self_context(sectx, sizeof(sectx)) == 0 && sectx[0])
			printf("%s\n", sectx);
		else
			printf("u:r:unlabeled:s0\n");
		return 0;
	}

	uid = getuid();
	gid = getgid();
	euid = geteuid();
	egid = getegid();

	if ((pwd = getpwuid(uid)) == NULL)
		printf("%s%d%s", "uid=", uid, " ");
	else
		printf("%s%d%s%s%s", "uid=", uid, "(", pwd->pw_name, ") ");

	if ((grp = getgrgid(gid)) == NULL)
		printf("%s%d%s", "gid=", gid, " ");
	else
		printf("%s%d%s%s%s", "gid=", gid, "(", grp->gr_name, ") ");

	if (uid != euid) {
		if ((pwd = getpwuid(euid)) != NULL)
			printf("%s%d%s%s%s", "euid=", euid, "(", pwd->pw_name, ") ");
		else
			printf("%s%d%s", "euid=", euid, " ");
	}

	if (gid != egid) {
		if ((grp = getgrgid(egid)) != NULL)
			printf("%s%d%s%s%s", "egid=", egid, "(", grp->gr_name, ") ");
		else
			printf("%s%d%s", "egid=", egid, " ");
	}

	if (get_self_context(sectx, sizeof(sectx)) == 0 && sectx[0])
		printf("context=%s", sectx);

	printf("\n");
	return 0;
}
