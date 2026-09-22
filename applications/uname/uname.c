#include <stdio.h>
#include <string.h>
#include <linux/utsname.h>

extern int uname(struct new_utsname *name);

#define F_SYSNAME  0x01
#define F_NODENAME 0x02
#define F_RELEASE  0x04
#define F_VERSION  0x08
#define F_MACHINE  0x10
#define F_ALL      (F_SYSNAME | F_NODENAME | F_RELEASE | F_VERSION | F_MACHINE)

static void
print_field(const char *s, int *first)
{
	if (!*first)
		printf(" ");
	printf("%s", s);
	*first = 0;
}

int
main(int argc, char **argv)
{
	struct new_utsname u;
	int flags = 0;
	int i, first = 1;

	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			const char *p = argv[i] + 1;
			while (*p) {
				switch (*p) {
				case 'a': flags |= F_ALL; break;
				case 's': flags |= F_SYSNAME; break;
				case 'n': flags |= F_NODENAME; break;
				case 'r': flags |= F_RELEASE; break;
				case 'v': flags |= F_VERSION; break;
				case 'm':
				case 'p': flags |= F_MACHINE; break;
				default:
					printf("Usage: uname [-asnrvmp]\n");
					return 1;
				}
				p++;
			}
		}
	}
	if (flags == 0)
		flags = F_SYSNAME;

	memset(&u, 0, sizeof(u));
	if (uname(&u) < 0) {
		strcpy(u.sysname, "Linux");
		strcpy(u.nodename, "six");
		strcpy(u.release, "2.0.11");
		strcpy(u.version, "#1 SIX");
		strcpy(u.machine, "i386");
	}

	if (flags & F_SYSNAME)
		print_field(u.sysname, &first);
	if (flags & F_NODENAME)
		print_field(u.nodename, &first);
	if (flags & F_RELEASE)
		print_field(u.release, &first);
	if (flags & F_VERSION)
		print_field(u.version, &first);
	if (flags & F_MACHINE)
		print_field(u.machine, &first);
	printf("\n");
	return 0;
}
