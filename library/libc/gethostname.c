#include <unistd.h>
#include <string.h>
#include <fcntl.h>

int gethostname(char *name, size_t len)
{
	int fd, n;
	if (!name || len == 0)
		return -1;

	fd = open("/etc/hostname", O_RDONLY);
	if (fd >= 0) {
		n = read(fd, name, len - 1);
		close(fd);
		if (n > 0) {
			while (n > 0 && (name[n - 1] == '\n' || name[n - 1] == '\r' || name[n - 1] == ' '))
				n--;
			name[n] = '\0';
			return 0;
		}
	}
	strncpy(name, "six", len);
	name[len - 1] = '\0';
	return 0;
}
