#ifndef _TERMIOS_H
#define _TERMIOS_H

#include <linux/termios.h>

int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
int tcflush(int fd, int queue_selector);

speed_t cfgetospeed(const struct termios *termios_p);
speed_t cfgetispeed(const struct termios *termios_p);
int cfsetospeed(struct termios *termios_p, speed_t speed);
int cfsetispeed(struct termios *termios_p, speed_t speed);

#define cfgetospeed(tp) ((tp)->c_cflag & CBAUD)
#define cfgetispeed(tp) ((tp)->c_cflag & CBAUD)
#define cfsetospeed(tp, sp) (((tp)->c_cflag = ((tp)->c_cflag & ~CBAUD) | ((sp) & CBAUD)), 0)
#define cfsetispeed(tp, sp) (((tp)->c_cflag = ((tp)->c_cflag & ~CBAUD) | ((sp) & CBAUD)), 0)

#endif
