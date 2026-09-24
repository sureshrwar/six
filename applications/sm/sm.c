/*
 * applications/sm/sm.c
 *
 * Android StorageManager CLI (/bin/sm) for SIX.
 *
 * Communicates with StorageManagerService ("mount" [android.os.storage.IStorageManager])
 * over /dev/binder:
 *   sm list-disks
 *   sm list-volumes
 *   sm mount <vol-id>
 *   sm unmount <vol-id>
 *   sm partition <disk-id> public|private
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/binder.h>

extern int errno;

#define ISM_LIST_DISKS			1
#define ISM_LIST_VOLUMES		2
#define ISM_MOUNT			3
#define ISM_UNMOUNT			4
#define ISM_PARTITION			5

static void usage(void)
{
	printf("Usage: sm <command> [args...]\n");
	printf("Commands:\n");
	printf("  sm list-disks                      List all adoptable/removable disks\n");
	printf("  sm list-volumes                    List all storage volumes & states\n");
	printf("  sm mount <vol-id>                  Mount a storage volume via Binder -> vold\n");
	printf("  sm unmount <vol-id>                Unmount a storage volume via Binder -> vold\n");
	printf("  sm partition <disk-id> public      Format disk as portable ext2 USB storage\n");
	printf("  sm partition <disk-id> ntfs        Format disk as portable NTFS USB storage (FUSE ntfs-3g)\n");
	printf("  sm partition <disk-id> private     Format disk as Adoptable Storage (dm-crypt ChaCha20-256)\n");
}

int main(int argc, char **argv)
{
	int bfd;
	struct binder_service_info sinfo;
	struct binder_ipc_msg msg;

	if (argc < 2) {
		usage();
		return 1;
	}

	bfd = open("/dev/binder", 2);
	if (bfd < 0) {
		printf("sm: cannot open /dev/binder (errno=%d)\n", errno);
		return 1;
	}

	memset(&sinfo, 0, sizeof(sinfo));
	strcpy(sinfo.name, "mount");
	if (ioctl(bfd, BINDER_IOC_LOOKUP_SVC, &sinfo) < 0 || sinfo.handle <= 0) {
		printf("sm: StorageManagerService ('mount') is not registered on /dev/binder\n");
		close(bfd);
		return 1;
	}

	memset(&msg, 0, sizeof(msg));
	msg.target_handle = sinfo.handle;
	msg.flags = 0;
	strcpy(msg.interface_token, "android.os.storage.IStorageManager");

	if (strcmp(argv[1], "list-disks") == 0) {
		msg.code = ISM_LIST_DISKS;
	} else if (strcmp(argv[1], "list-volumes") == 0) {
		msg.code = ISM_LIST_VOLUMES;
	} else if (strcmp(argv[1], "mount") == 0 && argc >= 3) {
		msg.code = ISM_MOUNT;
		strncpy(msg.data, argv[2], BINDER_MAX_DATA_SIZE - 1);
		msg.data_size = strlen(msg.data) + 1;
	} else if (strcmp(argv[1], "unmount") == 0 && argc >= 3) {
		msg.code = ISM_UNMOUNT;
		strncpy(msg.data, argv[2], BINDER_MAX_DATA_SIZE - 1);
		msg.data_size = strlen(msg.data) + 1;
	} else if (strcmp(argv[1], "partition") == 0 && argc >= 4) {
		msg.code = ISM_PARTITION;
		strncpy(msg.data, argv[3], BINDER_MAX_DATA_SIZE - 1);
		msg.data_size = strlen(msg.data) + 1;
	} else {
		usage();
		close(bfd);
		return 1;
	}

	if (ioctl(bfd, BINDER_IOC_TRANSACT, &msg) < 0) {
		printf("sm: Binder transaction failed (errno=%d)\n", errno);
		close(bfd);
		return 1;
	}

	if (msg.data[0])
		printf("%s\n", msg.data);

	close(bfd);
	return (msg.status == 0) ? 0 : 1;
}
