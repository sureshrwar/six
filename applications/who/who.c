#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/time.h>
#include <utmp.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>

extern int open(const char *pathname, int flags, ...);
extern int read(int fd, void *buf, unsigned int count);
extern int close(int fd);
extern char *ttyname(int fd);
extern char *ctime(const time_t *timep);
extern uid_t geteuid(void);

int
main(int argc, char **argv)
{
	int am_i = 0;
	const char *my_tty = NULL;
	struct utmp ut;
	int fd, printed = 0;

	if (argc >= 2 && (strcmp(argv[1], "-m") == 0 || argc == 3)) {
		am_i = 1;
		my_tty = ttyname(0);
		if (my_tty && strncmp(my_tty, "/dev/", 5) == 0)
			my_tty += 5;
	}

	fd = open(UTMP, O_RDONLY);
	if (fd >= 0) {
		while (read(fd, &ut, sizeof(ut)) == sizeof(ut)) {
			char user[9], line[13], tstr[32];
			time_t t;
			char *ct;

			if (ut.ut_type != USER_PROCESS || ut.ut_user[0] == '\0')
				continue;
			memcpy(user, ut.ut_user, 8);
			user[8] = '\0';
			memcpy(line, ut.ut_line, 12);
			line[12] = '\0';

			if (am_i && my_tty && strcmp(line, my_tty) != 0)
				continue;

			t = (time_t)ut.ut_time;
			ct = ctime(&t);
			if (ct && strlen(ct) >= 16) {
				memcpy(tstr, ct + 4, 12);
				tstr[12] = '\0';
			} else {
				strcpy(tstr, "?");
			}

			printf("%-8s %-12s %s\n", user, line, tstr);
			printed++;
		}
		close(fd);
	}

	if (printed == 0 && am_i) {
		struct passwd *pw = getpwuid(geteuid());
		printf("%-8s %-12s\n",
		       (pw && pw->pw_name) ? pw->pw_name : "root",
		       my_tty ? my_tty : "tty1");
	}
	return 0;
}
