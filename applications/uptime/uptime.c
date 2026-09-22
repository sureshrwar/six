#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

int
main(int argc, char **argv)
{
	char ubuf[128], lbuf[128];
	int fd, n;
	unsigned long up_secs = 0, days, hours, mins, secs;
	char l1[16] = "0.00", l5[16] = "0.00", l15[16] = "0.00";
	time_t now;
	struct tm *tm_p;

	(void)argc;
	(void)argv;

	fd = open("/proc/uptime", 0);
	if (fd >= 0) {
		n = read(fd, ubuf, sizeof(ubuf) - 1);
		close(fd);
		if (n > 0) {
			ubuf[n] = '\0';
			up_secs = (unsigned long)atol(ubuf);
		}
	}

	fd = open("/proc/loadavg", 0);
	if (fd >= 0) {
		n = read(fd, lbuf, sizeof(lbuf) - 1);
		close(fd);
		if (n > 0) {
			lbuf[n] = '\0';
			sscanf(lbuf, "%15s %15s %15s", l1, l5, l15);
		}
	}

	days  = up_secs / 86400UL;
	hours = (up_secs % 86400UL) / 3600UL;
	mins  = (up_secs % 3600UL) / 60UL;
	secs  = up_secs % 60UL;

	now = time(NULL);
	tm_p = localtime(&now);
	if (tm_p)
		printf(" %02d:%02d:%02d up ", tm_p->tm_hour, tm_p->tm_min, tm_p->tm_sec);
	else
		printf(" up ");

	if (days > 0)
		printf("%lu day%s, ", days, days == 1 ? "" : "s");
	if (hours > 0)
		printf("%lu:%02lu, ", hours, mins);
	else
		printf("%lu min (%lu s), ", mins, secs);

	printf("1 user,  load average: %s, %s, %s\n", l1, l5, l15);
	return 0;
}
