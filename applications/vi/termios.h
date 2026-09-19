#ifndef _VI_TERMIOS_WRAPPER_H
#define _VI_TERMIOS_WRAPPER_H
#include <linux/ioctl.h>
#include <linux/termios.h>
#define cfgetospeed(tp) ((tp)->c_cflag & CBAUD)
#define tcsetattr(fd, act, tp) ioctl(fd, TCSETS, tp)
#endif
