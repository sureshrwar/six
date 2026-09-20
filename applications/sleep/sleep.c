/*
 * sleep - suspend execution for an interval
 *
 * Usage: sleep NUMBER[SUFFIX]...
 *
 * Supported suffixes:
 *   s - seconds (default)
 *   m - minutes
 *   h - hours
 *   d - days
 *
 * Supports decimal values (e.g. 0.5, 1.5s).
 * Multiple arguments are summed (e.g. sleep 1 2 sleeps for 3 seconds).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/errno.h>

extern int nanosleep(struct timespec *rqtp, struct timespec *rmtp);
extern int select(int n, void *r, void *w, void *e, struct timeval *tv);

static void usage(void)
{
	printf("Usage: sleep NUMBER[s|m|h|d]...\n");
	printf("Pause for NUMBER seconds (or s=seconds, m=minutes, h=hours, d=days).\n");
	printf("NUMBER may be an arbitrary floating point or integer number.\n");
}

static int parse_duration(const char *s, long *out_sec, long *out_nsec)
{
	const char *p = s;
	long sec = 0;
	long nsec = 0;
	long frac_div = 1;
	int in_frac = 0;
	int has_digits = 0;
	long mult = 1;

	while (*p == ' ' || *p == '\t') p++;

	if (*p == '-' || *p == '\0')
		return -1;

	while (*p) {
		if (*p >= '0' && *p <= '9') {
			has_digits = 1;
			if (!in_frac) {
				sec = sec * 10 + (*p - '0');
			} else {
				if (frac_div < 1000000000L) {
					nsec = nsec * 10 + (*p - '0');
					frac_div *= 10;
				}
			}
			p++;
		} else if (*p == '.') {
			if (in_frac) return -1;
			in_frac = 1;
			p++;
		} else {
			break;
		}
	}

	if (!has_digits)
		return -1;

	while (frac_div < 1000000000L) {
		nsec *= 10;
		frac_div *= 10;
	}

	if (*p == 's' || *p == 'S') {
		mult = 1;
		p++;
	} else if (*p == 'm' || *p == 'M') {
		mult = 60;
		p++;
	} else if (*p == 'h' || *p == 'H') {
		mult = 3600;
		p++;
	} else if (*p == 'd' || *p == 'D') {
		mult = 86400;
		p++;
	}

	while (*p == ' ' || *p == '\t') p++;
	if (*p != '\0')
		return -1;

	sec *= mult;
	nsec *= mult;
	sec += nsec / 1000000000L;
	nsec %= 1000000000L;

	*out_sec = sec;
	*out_nsec = nsec;
	return 0;
}

int main(int argc, char **argv)
{
	long total_sec = 0;
	long total_nsec = 0;
	struct timespec req, rem;
	int i;

	if (argc < 2) {
		printf("sleep: missing operand\n");
		printf("Try 'sleep --help' for more information.\n");
		return 1;
	}

	if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
		usage();
		return 0;
	}

	for (i = 1; i < argc; i++) {
		long s = 0, ns = 0;
		if (parse_duration(argv[i], &s, &ns) < 0) {
			printf("sleep: invalid time interval '%s'\n", argv[i]);
			return 1;
		}
		total_sec += s;
		total_nsec += ns;
		total_sec += total_nsec / 1000000000L;
		total_nsec %= 1000000000L;
	}

	if (total_sec == 0 && total_nsec == 0)
		return 0;

	req.tv_sec = total_sec;
	req.tv_nsec = total_nsec;

	if (nanosleep(&req, &rem) < 0) {
		struct timeval tv;
		tv.tv_sec = req.tv_sec;
		tv.tv_usec = req.tv_nsec / 1000;
		select(0, NULL, NULL, NULL, &tv);
	}

	return 0;
}
