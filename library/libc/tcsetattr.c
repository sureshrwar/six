#include <linux/ioctl.h>
#include <linux/termios.h>


int tcsetattr(int fd, struct termios *termios_p)
{
  return(ioctl(fd, TCSETS, termios_p));
}

