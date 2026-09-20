/*
 * df.c - Report filesystem disk space usage for SIX (/bin/df)
 */

#include <stdio.h>
#include <stdlib.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <asm/statfs.h>

static void format_human(long kbytes, char *out)
{
	if (kbytes >= 1024 * 1024)
		sprintf(out, "%ld.%ldG", kbytes / (1024 * 1024), (kbytes % (1024 * 1024)) / (1024 * 100));
	else if (kbytes >= 1024)
		sprintf(out, "%ldM", kbytes / 1024);
	else
		sprintf(out, "%ldK", kbytes);
}

static int show_df(const char *path, int human)
{
	struct statfs s;
	long total_k, free_k, used_k, avail_k;
	int pct = 0;

	if (statfs((char *)path, &s) != 0) {
		perror(path);
		return 1;
	}

	total_k = (s.f_blocks * s.f_bsize) / 1024;
	free_k  = (s.f_bfree * s.f_bsize) / 1024;
	avail_k = (s.f_bavail * s.f_bsize) / 1024;
	used_k  = total_k - free_k;

	if (total_k > 0)
		pct = (int)((used_k * 100) / total_k);

	if (human) {
		char s_tot[16], s_used[16], s_avail[16];
		format_human(total_k, s_tot);
		format_human(used_k, s_used);
		format_human(avail_k, s_avail);
		printf("%-15s %8s %8s %8s %4d%% %s\n",
		       "/dev/hda", s_tot, s_used, s_avail, pct, path);
	} else {
		printf("%-15s %9ld %9ld %9ld %4d%% %s\n",
		       "/dev/hda", total_k, used_k, avail_k, pct, path);
	}
	return 0;
}

int main(int argc, char **argv)
{
	int human = 0;
	const char *path = "/";
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0) human = 1;
		else path = argv[i];
	}

	if (human)
		printf("%-15s %8s %8s %8s %5s %s\n",
		       "Filesystem", "Size", "Used", "Avail", "Use%", "Mounted on");
	else
		printf("%-15s %9s %9s %9s %5s %s\n",
		       "Filesystem", "1K-blocks", "Used", "Available", "Use%", "Mounted on");

	return show_df(path, human);
}
