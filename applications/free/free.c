#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static unsigned long
parse_val(const char *buf, const char *key)
{
	const char *p = strstr(buf, key);
	if (!p)
		return 0;
	p += strlen(key);
	while (*p == ' ' || *p == '\t' || *p == ':')
		p++;
	return (unsigned long)atol(p);
}

int
main(int argc, char **argv)
{
	char buf[1024];
	int fd, n, i;
	int shift = 10; /* default: KiB (-k) */
	unsigned long total = 0, used = 0, free_m = 0, shared = 0, buffers = 0, cached = 0;
	unsigned long stotal = 0, sused = 0, sfree = 0;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-b") == 0)
			shift = 0;
		else if (strcmp(argv[i], "-k") == 0)
			shift = 10;
		else if (strcmp(argv[i], "-m") == 0)
			shift = 20;
	}

	fd = open("/proc/meminfo", 0);
	if (fd < 0) {
		perror("/proc/meminfo");
		return 1;
	}
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return 1;
	buf[n] = '\0';

	/* Linux 2.0 /proc/meminfo has both summary lines (Mem: total used free shared buffers cached)
	 * and key: value lines (MemTotal:, MemFree:, etc.). Handle both cleanly.
	 */
	{
		const char *m = strstr(buf, "Mem:");
		if (m) {
			sscanf(m + 4, "%lu %lu %lu %lu %lu %lu",
			       &total, &used, &free_m, &shared, &buffers, &cached);
		} else {
			total   = parse_val(buf, "MemTotal") * 1024UL;
			free_m  = parse_val(buf, "MemFree") * 1024UL;
			shared  = parse_val(buf, "MemShared") * 1024UL;
			buffers = parse_val(buf, "Buffers") * 1024UL;
			cached  = parse_val(buf, "Cached") * 1024UL;
			if (total >= free_m)
				used = total - free_m;
		}
		m = strstr(buf, "Swap:");
		if (m) {
			sscanf(m + 5, "%lu %lu %lu", &stotal, &sused, &sfree);
		} else {
			stotal = parse_val(buf, "SwapTotal") * 1024UL;
			sfree  = parse_val(buf, "SwapFree") * 1024UL;
			if (stotal >= sfree)
				sused = stotal - sfree;
		}
	}

	printf("             total       used       free     shared    buffers     cached\n");
	printf("Mem:    %10lu %10lu %10lu %10lu %10lu %10lu\n",
	       total >> shift, used >> shift, free_m >> shift,
	       shared >> shift, buffers >> shift, cached >> shift);
	printf("-/+ buffers/cache: %10lu %10lu\n",
	       (used > (buffers + cached) ? (used - buffers - cached) : 0) >> shift,
	       (free_m + buffers + cached) >> shift);
	printf("Swap:   %10lu %10lu %10lu\n",
	       stotal >> shift, sused >> shift, sfree >> shift);
	return 0;
}
