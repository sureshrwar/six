#include <termios.h>

#undef cfgetospeed
#undef cfgetispeed
#undef cfsetospeed
#undef cfsetispeed

speed_t cfgetospeed(const struct termios *tp)
{
	return tp ? (tp->c_cflag & CBAUD) : 0;
}

speed_t cfgetispeed(const struct termios *tp)
{
	return tp ? (tp->c_cflag & CBAUD) : 0;
}

int cfsetospeed(struct termios *tp, speed_t speed)
{
	if (!tp) return -1;
	tp->c_cflag = (tp->c_cflag & ~CBAUD) | (speed & CBAUD);
	return 0;
}

int cfsetispeed(struct termios *tp, speed_t speed)
{
	if (!tp) return -1;
	tp->c_cflag = (tp->c_cflag & ~CBAUD) | (speed & CBAUD);
	return 0;
}
