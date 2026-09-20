#include <stdarg.h>
#include <linux/ioctl.h>
#include <linux/termios.h>

extern int ioctl(int fd, int request, ...);

int tcsetattr(int fd, ...)
{
	va_list ap;
	unsigned long arg2, arg3;
	struct termios *tp;
	int cmd;

	va_start(ap, fd);
	arg2 = va_arg(ap, unsigned long);

	/*
	 * If arg2 < 4096, the caller used the 3-argument POSIX form:
	 * tcsetattr(fd, optional_actions, termios_p)
	 * Otherwise, it's the legacy 2-argument SIX form:
	 * tcsetattr(fd, termios_p)
	 */
	if (arg2 < 4096) {
		arg3 = va_arg(ap, unsigned long);
		tp = (struct termios *)arg3;
		if (arg2 == TCSADRAIN)
			cmd = TCSETSW;
		else if (arg2 == TCSAFLUSH)
			cmd = TCSETSF;
		else
			cmd = TCSETS;
	} else {
		tp = (struct termios *)arg2;
		cmd = TCSETS;
	}
	va_end(ap);

	return ioctl(fd, cmd, tp);
}
