#include <linux/fcntl.h>
#include <linux/signal.h>
#include <linux/unistd.h>
#include <sys/mount.h>

static void umount_all_from_proc(void)
{
	char buf[4096];
	char *mnts[32];
	int fd, n, count = 0, i;
	char *p;

	fd = open("/proc/mounts", O_RDONLY);
	if (fd >= 0) {
		n = read(fd, buf, sizeof(buf) - 1);
		close(fd);
		if (n > 0) {
			buf[n] = '\0';
			p = buf;
			while (*p && count < 32) {
				char *line = p;
				char *mp;

				while (*p && *p != '\n')
					p++;
				if (*p == '\n')
					*p++ = '\0';

				/* Skip first token (device) */
				while (*line == ' ' || *line == '\t')
					line++;
				while (*line && *line != ' ' && *line != '\t')
					line++;
				while (*line == ' ' || *line == '\t')
					line++;
				if (!*line)
					continue;

				/* Second token is mountpoint */
				mp = line;
				while (*line && *line != ' ' && *line != '\t')
					line++;
				*line = '\0';
				mnts[count++] = mp;
			}
		}
	}

	if (count > 0) {
		for (i = count - 1; i >= 0; i--)
			umount(mnts[i]);
	} else {
		umount("/");
	}
}

int main(void)
{
	chdir("/");
	sync();
	umount_all_from_proc();
	sync();
	kill(1, SIGTERM);
	reboot();
	return 0;
}
