#include <linux/signal.h>
#include <linux/unistd.h>
#include <sys/mount.h>

int main(void)
{
	chdir("/");
	sync();
	umount("/aux/storage-1");
	umount("/proc");
	umount("/");
	sync();
	kill(1, SIGTERM);
	reboot();
	return 0;
}
