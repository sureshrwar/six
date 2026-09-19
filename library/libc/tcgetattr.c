#include <linux/ioctl.h>
#include <linux/termios.h>


int tcgetattr(int fd, struct termios *termios_p)
{
  return(ioctl(fd, TCGETS, termios_p));
}

