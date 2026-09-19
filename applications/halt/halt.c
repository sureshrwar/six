#include <linux/signal.h>

main()
{
	kill(1, SIGTERM);
	reboot();
}
