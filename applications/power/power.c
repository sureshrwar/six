/*
 * applications/power/power.c
 *
 * Android PowerManager (IPowerManager) & SystemSuspend (ISystemSuspend)
 * CLI utility for SIX (`power` / `svc power`).
 *
 * Commands:
 *   power status                 Show PowerManagerService, SystemSuspend, & WakeLock state
 *   power sleep [timeout]        Call IPowerManager::goToSleep() (release Display wakelock
 *                                and arm SystemSuspend opportunistic autosuspend)
 *   power wakeup                 Call IPowerManager::wakeUp() (re-acquire Display wakelock)
 *   power lock <name>            Acquire userspace wakelock via IPowerManager & /sys/power/wake_lock
 *   power unlock <name>          Release userspace wakelock via IPowerManager & /sys/power/wake_unlock
 *                                (automatically suspends if last wakelock while autosuspend armed)
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/binder.h>

static int write_sysfs(const char *path, const char *val)
{
	int fd = open(path, 1);
	int n;
	if (fd < 0)
		return -1;
	n = write(fd, val, strlen(val));
	close(fd);
	return n;
}

static void print_file(const char *path)
{
	int fd = open(path, 0);
	char buf[1024];
	int n;
	if (fd < 0)
		return;
	while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
		buf[n] = '\0';
		printf("%s", buf);
	}
	close(fd);
}

static int call_power_binder(unsigned int code, const char *arg, char *out, int out_len)
{
	int bfd;
	struct binder_service_info sinfo;
	struct binder_ipc_msg msg;

	bfd = open("/dev/binder", 2);
	if (bfd < 0)
		return -1;

	memset(&sinfo, 0, sizeof(sinfo));
	strcpy(sinfo.name, "power");
	if (ioctl(bfd, BINDER_IOC_LOOKUP_SVC, &sinfo) < 0 || sinfo.handle <= 0) {
		close(bfd);
		return -1;
	}

	memset(&msg, 0, sizeof(msg));
	msg.target_handle = sinfo.handle;
	msg.code = code;
	strcpy(msg.interface_token, "android.os.IPowerManager");
	if (arg && arg[0]) {
		strncpy(msg.data, arg, BINDER_MAX_DATA_SIZE - 1);
		msg.data_size = strlen(msg.data) + 1;
	}
	if (ioctl(bfd, BINDER_IOC_TRANSACT, &msg) < 0) {
		close(bfd);
		return -1;
	}
	if (out && out_len > 0) {
		strncpy(out, msg.data, out_len - 1);
		out[out_len - 1] = '\0';
	}
	close(bfd);
	return msg.status;
}

static void usage(void)
{
	printf("Usage:\n");
	printf("  power status                 Show PowerManagerService, SystemSuspend & WakeLocks\n");
	printf("  power sleep [timeout]        Turn Display OFF & arm Opportunistic Autosuspend\n");
	printf("                               (e.g. 'power sleep' or 'power sleep 200ms')\n");
	printf("  power wakeup                 Turn Display ON (IPowerManager::wakeUp)\n");
	printf("  power lock <name>            Acquire PARTIAL_WAKE_LOCK <name>\n");
	printf("  power unlock <name>          Release PARTIAL_WAKE_LOCK <name>\n");
}

int main(int argc, char **argv)
{
	char reply[256];

	if (argc < 2 || strcmp(argv[1], "status") == 0) {
		reply[0] = '\0';
		if (call_power_binder(1, NULL, reply, sizeof(reply)) == 0 && reply[0])
			printf("[Binder IPowerManager] %s\n", reply);
		printf("--- /sys/power/suspend_stats ---\n");
		print_file("/sys/power/suspend_stats");
		printf("--- /proc/wakelocks ---\n");
		print_file("/proc/wakelocks");
		return 0;
	}

	if (strcmp(argv[1], "sleep") == 0) {
		if (argc >= 3 && argv[2][0]) {
			write_sysfs("/sys/power/wakealarm", argv[2]);
		}
		/*
		 * Release PowerManagerService.Display directly in the foreground
		 * process so that if no PARTIAL_WAKE_LOCKs are held, the terminal
		 * waits synchronously across PSCI_SYSTEM_SUSPEND until wake.
		 */
		write_sysfs("/sys/power/wake_unlock", "PowerManagerService.Display");
		return 0;
	}

	if (strcmp(argv[1], "wakeup") == 0) {
		reply[0] = '\0';
		if (call_power_binder(5, NULL, reply, sizeof(reply)) == 0 && reply[0])
			printf("%s\n", reply);
		else
			write_sysfs("/sys/power/wake_lock", "PowerManagerService.Display");
		return 0;
	}

	if (strcmp(argv[1], "lock") == 0 && argc >= 3) {
		reply[0] = '\0';
		if (call_power_binder(2, argv[2], reply, sizeof(reply)) == 0 && reply[0])
			printf("%s\n", reply);
		else
			write_sysfs("/sys/power/wake_lock", argv[2]);
		return 0;
	}

	if (strcmp(argv[1], "unlock") == 0 && argc >= 3) {
		/*
		 * Release the wakelock directly on /sys/power/wake_unlock so that if
		 * this is the last blocking wakelock while Display is OFF, the current
		 * process synchronously enters SystemSuspend opportunistic suspend.
		 */
		write_sysfs("/sys/power/wake_unlock", argv[2]);
		return 0;
	}

	usage();
	return 1;
}
