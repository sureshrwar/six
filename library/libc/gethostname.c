#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <linux/utsname.h>

extern int uname(struct new_utsname *name);

int gethostname(char *name, size_t len)
{
	struct new_utsname u;
	if (!name || len == 0) {
		errno = EINVAL;
		return -1;
	}
	if (uname(&u) == 0 && u.nodename[0] != '\0') {
		strncpy(name, u.nodename, len - 1);
		name[len - 1] = '\0';
		return 0;
	}
	strncpy(name, "black", len - 1);
	name[len - 1] = '\0';
	return 0;
}
