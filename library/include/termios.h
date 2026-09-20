#ifndef _TERMIOS_H
#define _TERMIOS_H

#include <linux/termios.h>

int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);

#endif
