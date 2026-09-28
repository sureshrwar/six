/*
 * servicemanager.c - Android Binder Context Manager (handle 0) for SIX
 * Claims BINDER_SET_CONTEXT_MGR (handle 0), registers built-in services,
 * and serves SVC_MGR_ADD_SERVICE / SVC_MGR_GET_SERVICE and RPC calls.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/unistd.h>
#include <linux/binder.h>
#include <android/os/IServiceManager.h>
#include <android/os/IPowerManager.h>
#include <android/os/ISystemInfoService.h>

extern int open(const char *pathname, int flags, ...);
extern int close(int fd);
extern int ioctl(int fd, int request, ...);
extern int getpid(void);

struct sm_entry {
	int in_use;
	int handle;
	int owner_pid;
	int owner_uid;
	char name[BINDER_MAX_NAME_LEN];
	char descriptor[BINDER_MAX_DESC_LEN];
};

static struct sm_entry registry[BINDER_MAX_SERVICES];
static int power_wakelocks = 0;

static struct sm_entry *sm_find(const char *name)
{
	int i;

	for (i = 0; i < BINDER_MAX_SERVICES; i++) {
		if (registry[i].in_use && strcmp(registry[i].name, name) == 0)
			return &registry[i];
	}
	return NULL;
}

static int sm_register_local(int bfd, const char *name, const char *descriptor)
{
	struct binder_service_info info;
	int i;

	memset(&info, 0, sizeof(info));
	strncpy(info.name, name, sizeof(info.name) - 1);
	strncpy(info.descriptor, descriptor, sizeof(info.descriptor) - 1);

	if (ioctl(bfd, BINDER_IOC_REGISTER_SVC, &info) < 0)
		return -1;

	for (i = 0; i < BINDER_MAX_SERVICES; i++) {
		if (!registry[i].in_use) {
			registry[i].in_use = 1;
			registry[i].handle = info.handle;
			registry[i].owner_pid = info.owner_pid;
			registry[i].owner_uid = info.owner_uid;
			strcpy(registry[i].name, name);
			strcpy(registry[i].descriptor, descriptor);
			break;
		}
	}
	return info.handle;
}

int main(int argc, char **argv)
{
	int bfd;
	struct binder_version ver;
	int max_threads = 15;
	int h_power, h_sysinfo;

	(void)argc;
	(void)argv;

	bfd = open("/dev/binder", O_RDWR, 0);
	if (bfd < 0) {
		fprintf(stderr, "servicemanager: cannot open /dev/binder\n");
		return 1;
	}

	memset(&ver, 0, sizeof(ver));
	if (ioctl(bfd, BINDER_VERSION, &ver) < 0 ||
	    ver.protocol_version != BINDER_CURRENT_PROTOCOL_VERSION) {
		fprintf(stderr, "servicemanager: binder protocol mismatch\n");
		close(bfd);
		return 1;
	}

	ioctl(bfd, BINDER_SET_MAX_THREADS, &max_threads);

	if (ioctl(bfd, BINDER_SET_CONTEXT_MGR, 0) < 0) {
		fprintf(stderr, "servicemanager: failed to become context manager (handle 0)\n");
		close(bfd);
		return 1;
	}

	/* Register built-in system services hosted by servicemanager */
	h_power = sm_register_local(bfd, IPOWERMANAGER_SERVICE_NAME, IPOWERMANAGER_DESCRIPTOR);
	h_sysinfo = sm_register_local(bfd, ISYSTEMINFO_SERVICE_NAME, ISYSTEMINFO_DESCRIPTOR);

	printf("[servicemanager] Context Manager ready on /dev/binder (pid=%d, handle=0)\n",
	       getpid());
	fflush(stdout);

	/* Main Binder transaction looper */
	while (1) {
		struct binder_ipc_msg msg;
		int rc;

		memset(&msg, 0, sizeof(msg));
		rc = ioctl(bfd, BINDER_IOC_RECV, &msg);
		if (rc < 0)
			continue;

		/* 1. Context Manager (Handle 0) IServiceManager transactions */
		if (msg.target_handle == BINDER_CONTEXT_MGR_HANDLE) {
			if (msg.code == SVC_MGR_ADD_SERVICE) {
				struct sm_entry *e = sm_find(msg.data);
				if (!e) {
					int i;
					for (i = 0; i < BINDER_MAX_SERVICES; i++) {
						if (!registry[i].in_use) {
							e = &registry[i];
							e->in_use = 1;
							break;
						}
					}
				}
				if (e) {
					e->handle = msg.reply_handle;
					e->owner_pid = msg.sender_pid;
					e->owner_uid = msg.sender_euid;
					strncpy(e->name, msg.data, BINDER_MAX_NAME_LEN - 1);
					strncpy(e->descriptor, msg.interface_token,
						BINDER_MAX_DESC_LEN - 1);
					msg.status = 0;
				} else {
					msg.status = -ENOMEM;
				}
				snprintf(msg.data, sizeof(msg.data),
					 "registered %s (handle=%d)", e ? e->name : "?", msg.reply_handle);
				msg.data_size = strlen(msg.data) + 1;
			} else if (msg.code == SVC_MGR_CHECK_SERVICE ||
				   msg.code == SVC_MGR_GET_SERVICE) {
				struct binder_service_info sinfo;
				memset(&sinfo, 0, sizeof(sinfo));
				strncpy(sinfo.name, msg.data, sizeof(sinfo.name) - 1);
				if (ioctl(bfd, BINDER_IOC_LOOKUP_SVC, &sinfo) == 0) {
					msg.status = 0;
					msg.reply_handle = sinfo.handle;
					strncpy(msg.interface_token, sinfo.descriptor,
						sizeof(msg.interface_token) - 1);
					snprintf(msg.data, sizeof(msg.data),
						 "%s", sinfo.descriptor);
					msg.data_size = strlen(msg.data) + 1;
				} else {
					msg.status = -ENOENT;
					msg.reply_handle = -1;
					msg.data_size = 0;
				}
			} else if (msg.code == PING_TRANSACTION) {
				msg.status = 0;
				strcpy(msg.data, "PONG");
				msg.data_size = 5;
			} else {
				msg.status = 0;
			}
		} else if (msg.target_handle == h_power) {
			/* Built-in IPowerManager service */
			if (msg.code == IPM_GET_STATUS) {
				char active_wls[128] = {0};
				int wl_cnt = 0, i;
				int wfd = open("/sys/power/wake_lock", 0, 0);
				if (wfd >= 0) {
					int n = read(wfd, active_wls, sizeof(active_wls) - 1);
					if (n > 0) {
						if (active_wls[n - 1] == '\n')
							active_wls[n - 1] = '\0';
					}
					close(wfd);
				}
				if (active_wls[0]) {
					wl_cnt = 1;
					for (i = 0; active_wls[i]; i++) {
						if (active_wls[i] == ' ')
							wl_cnt++;
					}
					if (strstr(active_wls, "PowerManagerService.Display"))
						wl_cnt--;
				}
				snprintf(msg.data, sizeof(msg.data),
					 "PowerState=%s interactive=%s wakelocks=%d active=[%s] caller_pid=%d",
					 strstr(active_wls, "PowerManagerService.Display") ? "AWAKE" : "DOZE_AUTOSUSPEND",
					 strstr(active_wls, "PowerManagerService.Display") ? "true" : "false",
					 wl_cnt,
					 active_wls[0] ? active_wls : "none",
					 msg.sender_pid);
				msg.data_size = strlen(msg.data) + 1;
				msg.status = 0;
			} else if (msg.code == IPM_ACQUIRE_WAKE_LOCK) {
				const char *wl_name = (msg.data_size > 1 && msg.data[0]) ? msg.data : "PowerManagerService.WakeLocks";
				int wfd = open("/sys/power/wake_lock", 1, 0);
				if (wfd >= 0) {
					write(wfd, wl_name, strlen(wl_name));
					close(wfd);
				}
				power_wakelocks++;
				snprintf(msg.data, sizeof(msg.data),
					 "WakeLock '%s' acquired (total=%d) by pid=%d",
					 wl_name, power_wakelocks, msg.sender_pid);
				msg.data_size = strlen(msg.data) + 1;
				msg.status = 0;
			} else if (msg.code == IPM_RELEASE_WAKE_LOCK) {
				const char *wl_name = (msg.data_size > 1 && msg.data[0]) ? msg.data : "PowerManagerService.WakeLocks";
				int ufd = open("/sys/power/wake_unlock", 1, 0);
				if (ufd >= 0) {
					write(ufd, wl_name, strlen(wl_name));
					close(ufd);
				}
				if (power_wakelocks > 0)
					power_wakelocks--;
				snprintf(msg.data, sizeof(msg.data),
					 "WakeLock '%s' released (remaining=%d) by pid=%d",
					 wl_name, power_wakelocks, msg.sender_pid);
				msg.data_size = strlen(msg.data) + 1;
				msg.status = 0;
			} else if (msg.code == IPM_GO_TO_SLEEP) {
				/* IPowerManager::goToSleep -> release PowerManagerService.Display */
				int ufd = open("/sys/power/wake_unlock", 1, 0);
				if (ufd >= 0) {
					write(ufd, "PowerManagerService.Display", 27);
					close(ufd);
				}
				snprintf(msg.data, sizeof(msg.data),
					 "IPowerManager::goToSleep completed (Display released -> Autosuspend)");
				msg.data_size = strlen(msg.data) + 1;
				msg.status = 0;
			} else if (msg.code == IPM_WAKE_UP) {
				/* IPowerManager::wakeUp -> acquire PowerManagerService.Display */
				int wfd = open("/sys/power/wake_lock", 1, 0);
				if (wfd >= 0) {
					write(wfd, "PowerManagerService.Display", 27);
					close(wfd);
				}
				snprintf(msg.data, sizeof(msg.data),
					 "IPowerManager::wakeUp completed (Display=ON, interactive=true)");
				msg.data_size = strlen(msg.data) + 1;
				msg.status = 0;
			} else if (msg.code == DUMP_TRANSACTION) {
				char active_wls[128] = {0};
				char wakeup_cnt[32] = {0};
				char pwr_states[64] = {0};
				int wl_cnt = 0, i, fd, n, is_awake;

				fd = open("/sys/power/wake_lock", 0, 0);
				if (fd >= 0) {
					n = read(fd, active_wls, sizeof(active_wls) - 1);
					if (n > 0 && active_wls[n - 1] == '\n')
						active_wls[n - 1] = '\0';
					close(fd);
				}
				fd = open("/sys/power/wakeup_count", 0, 0);
				if (fd >= 0) {
					n = read(fd, wakeup_cnt, sizeof(wakeup_cnt) - 1);
					if (n > 0 && wakeup_cnt[n - 1] == '\n')
						wakeup_cnt[n - 1] = '\0';
					close(fd);
				}
				fd = open("/sys/power/state", 0, 0);
				if (fd >= 0) {
					n = read(fd, pwr_states, sizeof(pwr_states) - 1);
					if (n > 0 && pwr_states[n - 1] == '\n')
						pwr_states[n - 1] = '\0';
					close(fd);
				}
				if (active_wls[0]) {
					wl_cnt = 1;
					for (i = 0; active_wls[i]; i++) {
						if (active_wls[i] == ' ')
							wl_cnt++;
					}
					if (strstr(active_wls, "PowerManagerService.Display"))
						wl_cnt--;
				}
				is_awake = (strstr(active_wls, "PowerManagerService.Display") != NULL);
				snprintf(msg.data, sizeof(msg.data),
					 "POWER MANAGER (dumpsys power)\n"
					 "  mWakefulness=%s\n"
					 "  mInteractive=%s\n"
					 "  mWakeLockSummary=0x%x (user_wakelocks=%d, total_acquired=%d)\n"
					 "  Kernel WakeLocks (/sys/power/wake_lock): %s\n"
					 "  Wakeup Count (/sys/power/wakeup_count): %s\n"
					 "  Supported States (/sys/power/state): %s",
					 is_awake ? "Awake" : "Doze",
					 is_awake ? "true" : "false",
					 wl_cnt > 0 ? 1 : 0, wl_cnt, power_wakelocks,
					 active_wls[0] ? active_wls : "none",
					 wakeup_cnt[0] ? wakeup_cnt : "0",
					 pwr_states[0] ? pwr_states : "freeze standby mem");
				msg.data_size = strlen(msg.data) + 1;
				msg.status = 0;
			} else {
				snprintf(msg.data, sizeof(msg.data),
					 "IPowerManager(code=%u)", msg.code);
				msg.data_size = strlen(msg.data) + 1;
				msg.status = 0;
			}
		} else if (msg.target_handle == h_sysinfo) {
			/* Built-in ISystemInfoService */
			if (msg.code == 1) {
				snprintf(msg.data, sizeof(msg.data),
					 "SIX 1.0 (Linux 2.0.11 + ext4 + dm-verity + overlayfs + binder) caller_pid=%d",
					 msg.sender_pid);
				msg.data_size = strlen(msg.data) + 1;
				msg.status = 0;
			} else if (msg.code == DUMP_TRANSACTION) {
				char uptime_str[64] = {0};
				int ufd = open("/proc/uptime", 0, 0);
				int svc_cnt = 0;
				struct binder_service_info sinfo;

				if (ufd >= 0) {
					int n = read(ufd, uptime_str, sizeof(uptime_str) - 1);
					if (n > 0 && uptime_str[n - 1] == '\n')
						uptime_str[n - 1] = '\0';
					close(ufd);
				}
				while (1) {
					memset(&sinfo, 0, sizeof(sinfo));
					sinfo.handle = svc_cnt;
					if (ioctl(bfd, BINDER_IOC_LIST_SVCS, &sinfo) < 0)
						break;
					svc_cnt++;
				}
				snprintf(msg.data, sizeof(msg.data),
					 "SYSTEM INFO SERVICE (dumpsys sysinfo)\n"
					 "  OS: SIX 1.0 (Linux 2.0.11 i386)\n"
					 "  Uptime (/proc/uptime): %s\n"
					 "  Subsystems: ext4, erofs, dm-verity, dm-crypt, overlayfs, fuse, binder, nvme, ufs\n"
					 "  Registered Binder Services: %d",
					 uptime_str[0] ? uptime_str : "0.00 0.00",
					 svc_cnt);
				msg.data_size = strlen(msg.data) + 1;
				msg.status = 0;
			} else {
				snprintf(msg.data, sizeof(msg.data),
					 "Echo from ISystemInfoService: '%s' (caller_pid=%d, uid=%d)",
					 msg.data_size ? msg.data : "",
					 msg.sender_pid, msg.sender_euid);
				msg.data_size = strlen(msg.data) + 1;
				msg.status = 0;
			}
		}

		if (!(msg.flags & TF_ONE_WAY))
			ioctl(bfd, BINDER_IOC_REPLY, &msg);
	}

	close(bfd);
	return 0;
}
