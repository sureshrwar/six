#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stat.h>
#include <time.h>

static void
format_mode(mode_t m, char *out)
{
	if (S_ISDIR(m)) out[0] = 'd';
	else if (S_ISLNK(m)) out[0] = 'l';
	else if (S_ISCHR(m)) out[0] = 'c';
	else if (S_ISBLK(m)) out[0] = 'b';
	else if (S_ISFIFO(m)) out[0] = 'p';
	else if (S_ISSOCK(m)) out[0] = 's';
	else out[0] = '-';

	out[1] = (m & 0400) ? 'r' : '-';
	out[2] = (m & 0200) ? 'w' : '-';
	out[3] = (m & 0100) ? ((m & 04000) ? 's' : 'x') : ((m & 04000) ? 'S' : '-');
	out[4] = (m & 0040) ? 'r' : '-';
	out[5] = (m & 0020) ? 'w' : '-';
	out[6] = (m & 0010) ? ((m & 02000) ? 's' : 'x') : ((m & 02000) ? 'S' : '-');
	out[7] = (m & 0004) ? 'r' : '-';
	out[8] = (m & 0002) ? 'w' : '-';
	out[9] = (m & 0001) ? ((m & 01000) ? 't' : 'x') : ((m & 01000) ? 'T' : '-');
	out[10] = '\0';
}

static const char *
file_type_name(mode_t m)
{
	if (S_ISREG(m)) return "regular file";
	if (S_ISDIR(m)) return "directory";
	if (S_ISLNK(m)) return "symbolic link";
	if (S_ISCHR(m)) return "character special file";
	if (S_ISBLK(m)) return "block special file";
	if (S_ISFIFO(m)) return "fifo";
	if (S_ISSOCK(m)) return "socket";
	return "weird file";
}

static void
format_time(time_t t, char *buf, int bufsz)
{
	struct tm *tm_p = localtime(&t);
	if (tm_p)
		snprintf(buf, bufsz, "%04d-%02d-%02d %02d:%02d:%02d",
		         tm_p->tm_year + 1900, tm_p->tm_mon + 1, tm_p->tm_mday,
		         tm_p->tm_hour, tm_p->tm_min, tm_p->tm_sec);
	else
		snprintf(buf, bufsz, "%lu", (unsigned long)t);
}

int
main(int argc, char **argv)
{
	int deref = 0, i = 1, rc = 0;

	if (i < argc && strcmp(argv[i], "-L") == 0) {
		deref = 1;
		i++;
	}
	if (i >= argc) {
		printf("Usage: stat [-L] FILE...\n");
		return 1;
	}

	for (; i < argc; i++) {
		struct stat st;
		char mstr[12], tbuf[32], ltarget[256];
		int r = deref ? stat(argv[i], &st) : lstat(argv[i], &st);
		if (r < 0) {
			perror(argv[i]);
			rc = 1;
			continue;
		}
		format_mode(st.st_mode, mstr);
		if (S_ISLNK(st.st_mode)) {
			int lr = readlink(argv[i], ltarget, sizeof(ltarget) - 1);
			if (lr > 0) {
				ltarget[lr] = '\0';
				printf("  File: %s -> %s\n", argv[i], ltarget);
			} else {
				printf("  File: %s\n", argv[i]);
			}
		} else {
			printf("  File: %s\n", argv[i]);
		}
		printf("  Size: %-10lu\tBlocks: %-10lu IO Block: %-6lu %s\n",
		       (unsigned long)st.st_size, (unsigned long)st.st_blocks,
		       (unsigned long)st.st_blksize, file_type_name(st.st_mode));
		if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
			printf("Device: %04xh/%ud\tInode: %-10lu  Links: %-5u Device type: %u,%u\n",
			       (unsigned int)st.st_dev, (unsigned int)st.st_dev,
			       (unsigned long)st.st_ino, (unsigned int)st.st_nlink,
			       ((unsigned int)st.st_rdev >> 8) & 0xff,
			       (unsigned int)st.st_rdev & 0xff);
		} else {
			printf("Device: %04xh/%ud\tInode: %-10lu  Links: %u\n",
			       (unsigned int)st.st_dev, (unsigned int)st.st_dev,
			       (unsigned long)st.st_ino, (unsigned int)st.st_nlink);
		}
		printf("Access: (%04o/%s)  Uid: (%5u/%8s)   Gid: (%5u/%8s)\n",
		       (unsigned int)(st.st_mode & 07777), mstr,
		       (unsigned int)st.st_uid, st.st_uid == 0 ? "root" : "user",
		       (unsigned int)st.st_gid, st.st_gid == 0 ? "root" : "group");
		format_time(st.st_atime, tbuf, sizeof(tbuf));
		printf("Access: %s\n", tbuf);
		format_time(st.st_mtime, tbuf, sizeof(tbuf));
		printf("Modify: %s\n", tbuf);
		format_time(st.st_ctime, tbuf, sizeof(tbuf));
		printf("Change: %s\n", tbuf);
	}
	return rc;
}
