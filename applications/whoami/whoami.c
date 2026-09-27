#include <linux/types.h>
#include <pwd.h>
#include <stdio.h>

extern uid_t geteuid(void);

int
main(void)
{
	uid_t euid = geteuid();
	struct passwd *pw = getpwuid(euid);

	if (pw && pw->pw_name) {
		printf("%s\n", pw->pw_name);
		return 0;
	}
	printf("%d\n", (int)euid);
	return 0;
}
