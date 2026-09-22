#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stat.h>
#include <linux/dirent.h>

extern int getdents(unsigned int fd, struct dirent *dirp, unsigned int count);

static int
is_digits(const char *s)
{
	if (!*s)
		return 0;
	while (*s) {
		if (*s < '0' || *s > '9')
			return 0;
		s++;
	}
	return 1;
}

static void
get_comm(const char *pidstr, char *comm, int commsz)
{
	char path[64], buf[128];
	int fd, n, i;

	snprintf(comm, commsz, "?");
	snprintf(path, sizeof(path), "/proc/%s/cmdline", pidstr);
	fd = open(path, 0);
	if (fd >= 0) {
		n = read(fd, buf, sizeof(buf) - 1);
		close(fd);
		if (n > 0) {
			const char *base;
			buf[n] = '\0';
			for (i = 0; i < n; i++) {
				if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == ' ') {
					buf[i] = '\0';
					break;
				}
			}
			base = strrchr(buf, '/');
			snprintf(comm, commsz, "%s", base ? base + 1 : buf);
			return;
		}
	}
	snprintf(path, sizeof(path), "/proc/%s/stat", pidstr);
	fd = open(path, 0);
	if (fd >= 0) {
		n = read(fd, buf, sizeof(buf) - 1);
		close(fd);
		if (n > 0) {
			char *lp, *rp;
			buf[n] = '\0';
			lp = strchr(buf, '(');
			rp = strrchr(buf, ')');
			if (lp && rp && rp > lp + 1) {
				int len = (int)(rp - lp - 1);
				if (len >= commsz)
					len = commsz - 1;
				for (i = 0; i < len; i++)
					comm[i] = lp[1 + i];
				comm[len] = '\0';
			}
		}
	}
}

static void
print_link_entry(const char *comm, const char *pidstr, const char *fdname, const char *lpath)
{
	char target[128];
	struct stat st;
	int r;
	const char *type = "REG";

	r = readlink(lpath, target, sizeof(target) - 1);
	if (r <= 0)
		return;
	target[r] = '\0';

	if (stat(lpath, &st) == 0) {
		if (S_ISDIR(st.st_mode)) type = "DIR";
		else if (S_ISCHR(st.st_mode)) type = "CHR";
		else if (S_ISBLK(st.st_mode)) type = "BLK";
		else if (S_ISFIFO(st.st_mode)) type = "FIFO";
		else if (S_ISSOCK(st.st_mode)) type = "SOCK";
		printf("%-10s %5s root %4s %6s %8lu %5lu %s\n",
		       comm, pidstr, fdname, type,
		       (unsigned long)st.st_size, (unsigned long)st.st_ino, target);
	} else {
		printf("%-10s %5s root %4s %6s %8s %5s %s\n",
		       comm, pidstr, fdname, "LINK", "-", "-", target);
	}
}

int
main(int argc, char **argv)
{
	char dbuf[512], fdbuf[512];
	int pfd, n;
	const char *filter_pid = (argc > 2 && strcmp(argv[1], "-p") == 0) ? argv[2] : NULL;

	printf("COMMAND      PID USER   FD   TYPE     SIZE  NODE NAME\n");

	pfd = open("/proc", 0);
	if (pfd < 0) {
		perror("/proc");
		return 1;
	}

	while ((n = getdents(pfd, (struct dirent *)dbuf, sizeof(dbuf))) > 0) {
		int off = 0;
		while (off < n) {
			struct dirent *de = (struct dirent *)(dbuf + off);
			off += de->d_reclen;
			if (de->d_ino == 0 || !is_digits(de->d_name))
				continue;
			if (filter_pid && atoi(de->d_name) != atoi(filter_pid))
				continue;
			{
				char comm[16], path[96];
				int fdfd, fn;
				get_comm(de->d_name, comm, sizeof(comm));
				snprintf(path, sizeof(path), "/proc/%s/cwd", de->d_name);
				print_link_entry(comm, de->d_name, "cwd", path);
				snprintf(path, sizeof(path), "/proc/%s/exe", de->d_name);
				print_link_entry(comm, de->d_name, "txt", path);

				snprintf(path, sizeof(path), "/proc/%s/fd", de->d_name);
				fdfd = open(path, 0);
				if (fdfd >= 0) {
					while ((fn = getdents(fdfd, (struct dirent *)fdbuf, sizeof(fdbuf))) > 0) {
						int foff = 0;
						while (foff < fn) {
							struct dirent *fde = (struct dirent *)(fdbuf + foff);
							foff += fde->d_reclen;
							if (fde->d_ino == 0 || !is_digits(fde->d_name))
								continue;
							snprintf(path, sizeof(path), "/proc/%s/fd/%s", de->d_name, fde->d_name);
							print_link_entry(comm, de->d_name, fde->d_name, path);
						}
					}
					close(fdfd);
				}
			}
		}
	}
	close(pfd);
	return 0;
}
