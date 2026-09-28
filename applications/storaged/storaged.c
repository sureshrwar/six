/*
 * applications/storaged/storaged.c
 *
 * C implementation of Android's StorageManagerService
 * ([android.os.storage.IStorageManager], registered on /dev/binder as "mount").
 *
 * Communicates with /bin/vold ([android.os.IVold]) over /dev/binder:
 *   - Receives IVoldListener callbacks (onDiskCreated, onVolumeCreated,
 *     onVolumeStateChanged, onDiskDestroyed) from vold.
 *   - Automatically triggers IVold::mount() over Binder when a new USB
 *     volume is created (auto-mount policy).
 *   - Services /bin/sm and `service call mount ...` queries (list-disks,
 *     list-volumes, mount, unmount, partition).
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/binder.h>
#include <android/os/storage/IStorageManager.h>
#include <android/os/IVold.h>
#include <android/os/IVoldListener.h>
#include <android/system/suspend/ISystemSuspend.h>

extern int errno;

struct sm_state {
	int usb_disk_present;
	char disk_id[24];
	char disk_model[40];
	char disk_flags[16];
	char vol_id[24];
	char vol_type[16];
	char vol_state[16];
	char vol_path[48];
	char vol_label[32];
	char vol_uuid[32];
	unsigned int callbacks_received;
};

static struct sm_state sm;

static void parse_pipe_fields(const char *src, char out[6][48])
{
	int f = 0, pos = 0, i;
	for (i = 0; i < 6; i++)
		out[i][0] = '\0';
	if (!src)
		return;
	for (i = 0; src[i] != '\0' && f < 6; i++) {
		if (src[i] == '|') {
			out[f][pos] = '\0';
			f++;
			pos = 0;
		} else if (pos < 47) {
			out[f][pos++] = src[i];
		}
	}
	if (f < 6)
		out[f][pos] = '\0';
}

static int call_vold(int bfd, unsigned int code, const char *arg,
		     char *out_reply, int max_reply)
{
	struct binder_service_info sinfo;
	struct binder_ipc_msg msg;

	memset(&sinfo, 0, sizeof(sinfo));
	strcpy(sinfo.name, "vold");
	if (ioctl(bfd, BINDER_IOC_LOOKUP_SVC, &sinfo) < 0 || sinfo.handle <= 0)
		return -1;

	memset(&msg, 0, sizeof(msg));
	msg.target_handle = sinfo.handle;
	msg.code = code;
	msg.flags = 0;
	strcpy(msg.interface_token, "android.os.IVold");
	if (arg && arg[0]) {
		strncpy(msg.data, arg, BINDER_MAX_DATA_SIZE - 1);
		msg.data_size = strlen(msg.data) + 1;
	}
	if (ioctl(bfd, BINDER_IOC_TRANSACT, &msg) < 0)
		return -1;

	if (out_reply && max_reply > 0) {
		strncpy(out_reply, msg.data, max_reply - 1);
		out_reply[max_reply - 1] = '\0';
	}
	return msg.status;
}

int main(int argc, char **argv)
{
	int bfd;
	int h_suspend = -1;
	struct binder_service_info svc;
	struct binder_ipc_msg msg, reply;

	memset(&sm, 0, sizeof(sm));
	mkdir("/data", 0755);
	mkdir("/data/media", 0755);
	mkdir("/data/media/0", 0755);

	bfd = open("/dev/binder", 2);
	if (bfd < 0) {
		printf("storaged: cannot open /dev/binder (errno=%d)\n", errno);
		return 1;
	}

	memset(&svc, 0, sizeof(svc));
	strcpy(svc.name, "mount");
	strcpy(svc.descriptor, "android.os.storage.IStorageManager");
	if (ioctl(bfd, BINDER_IOC_REGISTER_SVC, &svc) < 0) {
		printf("storaged: failed to register mount service\n");
		close(bfd);
		return 1;
	}

	{
		struct binder_service_info susp_svc;
		memset(&susp_svc, 0, sizeof(susp_svc));
		strcpy(susp_svc.name, "suspend");
		strcpy(susp_svc.descriptor, "android.system.suspend.ISystemSuspend");
		if (ioctl(bfd, BINDER_IOC_REGISTER_SVC, &susp_svc) == 0)
			h_suspend = susp_svc.handle;
	}

	printf("storaged: StorageManagerService started (pid=%d, handle=%d [android.os.storage.IStorageManager])\n",
	       getpid(), svc.handle);

	for (;;) {
		memset(&msg, 0, sizeof(msg));
		if (ioctl(bfd, BINDER_IOC_RECV, &msg) < 0)
			continue;

		memset(&reply, 0, sizeof(reply));
		reply.txn_id = msg.txn_id;
		reply.status = 0;

		if (h_suspend > 0 && msg.target_handle == h_suspend) {
			if (msg.code == DUMP_TRANSACTION) {
				char active_wls[128] = {0};
				char wakeup_cnt[32] = {0};
				int fd, n;
				fd = open("/sys/power/wake_lock", 0);
				if (fd >= 0) {
					n = read(fd, active_wls, sizeof(active_wls) - 1);
					if (n > 0 && active_wls[n - 1] == '\n')
						active_wls[n - 1] = '\0';
					close(fd);
				}
				fd = open("/sys/power/wakeup_count", 0);
				if (fd >= 0) {
					n = read(fd, wakeup_cnt, sizeof(wakeup_cnt) - 1);
					if (n > 0 && wakeup_cnt[n - 1] == '\n')
						wakeup_cnt[n - 1] = '\0';
					close(fd);
				}
				snprintf(reply.data, sizeof(reply.data),
					 "SystemSuspend (dumpsys suspend)\n"
					 "  Service: android.system.suspend.ISystemSuspend (pid=%d)\n"
					 "  Autosuspend: enabled (/sys/power/state)\n"
					 "  Wakeup Count: %s\n"
					 "  Active WakeLocks: %s",
					 getpid(),
					 wakeup_cnt[0] ? wakeup_cnt : "0",
					 active_wls[0] ? active_wls : "none");
			} else {
				snprintf(reply.data, sizeof(reply.data),
					 "ISystemSuspend[code=%u]: handled by storaged (pid=%d)",
					 msg.code, getpid());
			}
			reply.data_size = strlen(reply.data) + 1;
			if (!(msg.flags & TF_ONE_WAY))
				ioctl(bfd, BINDER_IOC_REPLY, &reply);
			continue;
		}

		/* Handle IVoldListener oneway callbacks from vold */
		if (msg.code == IVOLD_LISTENER_ON_DISK_CREATED) {
			char f[6][48];
			parse_pipe_fields(msg.data, f);
			sm.usb_disk_present = 1;
			strncpy(sm.disk_id, f[0][0] ? f[0] : "disk:8,0", 23);
			strncpy(sm.disk_model, f[1][0] ? f[1] : "SanDisk Ultra USB 3.0", 39);
			strncpy(sm.disk_flags, f[2][0] ? f[2] : "USB", 15);
			sm.callbacks_received++;
			continue;
		}
		if (msg.code == IVOLD_LISTENER_ON_VOLUME_CREATED) {
			char f[6][48];
			parse_pipe_fields(msg.data, f);
			sm.usb_disk_present = 1;
			strncpy(sm.vol_id, f[0][0] ? f[0] : "public:8,1", 23);
			strncpy(sm.vol_type, f[1][0] ? f[1] : "PUBLIC", 15);
			strncpy(sm.vol_state, f[2][0] ? f[2] : "UNMOUNTED", 15);
			strncpy(sm.vol_path, f[3][0] ? f[3] : "/mnt/media_rw/usb", 47);
			strncpy(sm.vol_label, f[4][0] ? f[4] : "SAN_DISK_USB", 31);
			strncpy(sm.vol_uuid, f[5][0] ? f[5] : "4A8F-9C21", 31);
			sm.callbacks_received++;
			/* Auto-mount policy: instruct vold over Binder to mount the new volume! */
			if (call_vold(bfd, IVOLD_MOUNT, sm.vol_id, NULL, 0) == 0) {
				strcpy(sm.vol_state, "MOUNTED");
			}
			continue;
		}
		if (msg.code == IVOLD_LISTENER_ON_VOLUME_STATE_CHANGED) {
			char f[6][48];
			parse_pipe_fields(msg.data, f);
			sm.usb_disk_present = 1;
			if (!sm.disk_id[0]) strcpy(sm.disk_id, "disk:8,0");
			if (!sm.disk_model[0]) strcpy(sm.disk_model, "SanDisk Ultra USB 3.0");
			if (!sm.disk_flags[0]) strcpy(sm.disk_flags, "USB");
			if (f[0][0]) strncpy(sm.vol_id, f[0], 23);
			if (f[1][0]) strncpy(sm.vol_type, f[1], 15);
			if (f[2][0]) strncpy(sm.vol_state, f[2], 15);
			if (f[3][0]) strncpy(sm.vol_path, f[3], 47);
			if (f[4][0]) strncpy(sm.vol_label, f[4], 31);
			if (f[5][0]) strncpy(sm.vol_uuid, f[5], 31);
			sm.callbacks_received++;
			continue;
		}
		if (msg.code == IVOLD_LISTENER_ON_DISK_DESTROYED) {
			sm.usb_disk_present = 0;
			strcpy(sm.vol_state, "REMOVED");
			sm.callbacks_received++;
			continue;
		}

		/* Handle synchronous IStorageManager RPCs from /bin/sm, /bin/dumpsys, or /bin/service */
		switch (msg.code) {
		case PING_TRANSACTION:
			sprintf(reply.data, "PONG from StorageManagerService (pid=%d, callbacks=%u)",
				getpid(), sm.callbacks_received);
			reply.data_size = strlen(reply.data) + 1;
			break;

		case DUMP_TRANSACTION:
			snprintf(reply.data, sizeof(reply.data),
				 "StorageManagerService (dumpsys mount)\n"
				 "  Primary Volume: emulated;0 (type=EMULATED state=MOUNTED path=/data/media/0)\n"
				 "  Removable Disk: %s (%s, flags=%s)\n"
				 "  Removable Vol : %s (type=%s state=%s path=%s uuid=%s)\n"
				 "  IVoldListener Callbacks: %u",
				 sm.usb_disk_present ? sm.disk_id : "none",
				 sm.usb_disk_present ? sm.disk_model : "disconnected",
				 sm.usb_disk_present ? sm.disk_flags : "-",
				 sm.usb_disk_present ? sm.vol_id : "none",
				 sm.usb_disk_present ? sm.vol_type : "-",
				 sm.usb_disk_present ? sm.vol_state : "REMOVED",
				 (sm.usb_disk_present && strcmp(sm.vol_state, "MOUNTED") == 0) ? sm.vol_path : "none",
				 sm.usb_disk_present ? sm.vol_uuid : "-",
				 sm.callbacks_received);
			reply.data_size = strlen(reply.data) + 1;
			break;

		case ISM_LIST_DISKS:
			if (!sm.usb_disk_present) {
				strcpy(reply.data, "(no adoptable/removable disks connected)");
			} else {
				sprintf(reply.data, "%s  [model=\"%s\" flags=%s dev=/dev/sda]",
					sm.disk_id, sm.disk_model, sm.disk_flags);
			}
			reply.data_size = strlen(reply.data) + 1;
			break;

		case ISM_LIST_VOLUMES:
			if (!sm.usb_disk_present) {
				strcpy(reply.data,
				       "emulated;0   mounted     null       /data/media/0 (Internal Shared Storage)");
			} else {
				sprintf(reply.data,
					"emulated;0   mounted     null       /data/media/0 (Internal Shared Storage)\n"
					"%-12s %-11s %-10s %s (label=%s type=%s)",
					sm.vol_id,
					strcmp(sm.vol_state, "MOUNTED") == 0 ? "mounted" : "unmounted",
					sm.vol_uuid,
					strcmp(sm.vol_state, "MOUNTED") == 0 ? sm.vol_path : "none",
					sm.vol_label, sm.vol_type);
			}
			reply.data_size = strlen(reply.data) + 1;
			break;

		case ISM_MOUNT:
			if (call_vold(bfd, IVOLD_MOUNT, msg.data, reply.data, sizeof(reply.data)) < 0)
				reply.status = -1;
			reply.data_size = strlen(reply.data) + 1;
			break;

		case ISM_UNMOUNT:
			if (call_vold(bfd, IVOLD_UNMOUNT, msg.data, reply.data, sizeof(reply.data)) < 0)
				reply.status = -1;
			reply.data_size = strlen(reply.data) + 1;
			break;

		case ISM_PARTITION:
			if (call_vold(bfd, IVOLD_PARTITION, msg.data, reply.data, sizeof(reply.data)) < 0)
				reply.status = -1;
			reply.data_size = strlen(reply.data) + 1;
			break;

		default:
			sprintf(reply.data, "IStorageManager[code=%u]: handled by storaged (pid=%d)",
				msg.code, getpid());
			reply.data_size = strlen(reply.data) + 1;
			break;
		}

		if (!(msg.flags & TF_ONE_WAY))
			ioctl(bfd, BINDER_IOC_REPLY, &reply);
	}

	close(bfd);
	return 0;
}
