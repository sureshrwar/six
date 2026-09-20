#include <stdlib.h>
#include <string.h>

char *cuserid(char *s)
{
	char *user = getenv("USER");
	if (!user)
		user = "root";
	if (s) {
		strcpy(s, user);
		return s;
	}
	return user;
}
