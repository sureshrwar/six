/*
 * service.c - Android Binder service inspection and RPC tool (/bin/service)
 *
 * Usage:
 *   service list
 *   service check <service_name>
 *   service call <service_name> <code> [s16 <str> | i32 <int> ...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/unistd.h>
#include <linux/binder.h>

extern int open(const char *pathname, int flags, ...);
extern int close(int fd);
extern int ioctl(int fd, int request, ...);

static void usage(void)
{
	printf("Usage: service [-h|-?]\n"
	       "       service list\n"
	       "       service check SERVICE\n"
	       "       service call SERVICE CODE [i32 INT | s16 STR] ...\n");
}

static void print_parcel_dump(const struct binder_ipc_msg *msg)
{
	unsigned int len = msg->data_size;
	unsigned int offset = 0;

	printf("Result: Parcel(\n");
	if (len == 0) {
		printf("  0x00000000: 00000000                             '....            ')\n");
		return;
	}

	while (offset < len) {
		unsigned int i;
		printf("  0x%08x: ", offset);
		for (i = 0; i < 16; i += 4) {
			if (offset + i < len) {
				unsigned int w = 0;
				unsigned int b;
				for (b = 0; b < 4 && (offset + i + b) < len; b++) {
					w |= ((unsigned int)(unsigned char)msg->data[offset + i + b]) << (b * 8);
				}
				printf("%08x ", w);
			} else {
				printf("         ");
			}
		}
		printf("'");
		for (i = 0; i < 16 && (offset + i) < len; i++) {
			unsigned char c = (unsigned char)msg->data[offset + i];
			putchar((c >= 32 && c <= 126) ? c : '.');
		}
		printf("'\n");
		offset += 16;
	}
	printf("  /* String: \"%s\" (status=%d, reply_handle=%d) */\n)\n",
	       msg->data, msg->status, msg->reply_handle);
}

int main(int argc, char **argv)
{
	int bfd;

	if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "-?") == 0) {
		usage();
		return 0;
	}

	bfd = open("/dev/binder", O_RDWR, 0);
	if (bfd < 0) {
		fprintf(stderr, "service: cannot open /dev/binder (is binder initialized?)\n");
		return 1;
	}

	if (strcmp(argv[1], "list") == 0) {
		struct binder_service_info info;
		int idx = 0, count = 0;

		while (1) {
			memset(&info, 0, sizeof(info));
			info.handle = idx;
			if (ioctl(bfd, BINDER_IOC_LIST_SVCS, &info) < 0)
				break;
			count++;
			idx++;
		}

		printf("Found %d services:\n", count);
		for (idx = 0; idx < count; idx++) {
			memset(&info, 0, sizeof(info));
			info.handle = idx;
			if (ioctl(bfd, BINDER_IOC_LIST_SVCS, &info) == 0) {
				printf("%d\t%s: [%s] (handle=%d, pid=%d)\n",
				       idx, info.name, info.descriptor,
				       info.handle, info.owner_pid);
			}
		}
		close(bfd);
		return 0;
	}

	if (strcmp(argv[1], "check") == 0) {
		struct binder_ipc_msg msg;
		if (argc < 3) {
			usage();
			close(bfd);
			return 1;
		}
		memset(&msg, 0, sizeof(msg));
		msg.target_handle = BINDER_CONTEXT_MGR_HANDLE;
		msg.code = SVC_MGR_CHECK_SERVICE;
		strncpy(msg.data, argv[2], sizeof(msg.data) - 1);
		msg.data_size = strlen(msg.data) + 1;

		if (ioctl(bfd, BINDER_IOC_TRANSACT, &msg) == 0 && msg.reply_handle > 0) {
			printf("Service %s: found (handle=%d, interface=[%s])\n",
			       argv[2], msg.reply_handle, msg.data);
		} else {
			printf("Service %s: not found\n", argv[2]);
		}
		close(bfd);
		return 0;
	}

	if (strcmp(argv[1], "call") == 0) {
		struct binder_service_info sinfo;
		struct binder_ipc_msg msg;
		int i, pos = 0;

		if (argc < 4) {
			usage();
			close(bfd);
			return 1;
		}

		memset(&sinfo, 0, sizeof(sinfo));
		strncpy(sinfo.name, argv[2], sizeof(sinfo.name) - 1);
		if (ioctl(bfd, BINDER_IOC_LOOKUP_SVC, &sinfo) < 0) {
			fprintf(stderr, "service: service '%s' does not exist\n", argv[2]);
			close(bfd);
			return 1;
		}

		memset(&msg, 0, sizeof(msg));
		msg.target_handle = sinfo.handle;
		msg.code = (unsigned int)atoi(argv[3]);
		strcpy(msg.interface_token, sinfo.descriptor);

		for (i = 4; i < argc; i++) {
			if ((strcmp(argv[i], "s16") == 0 || strcmp(argv[i], "i32") == 0) &&
			    i + 1 < argc) {
				i++;
			}
			if (pos > 0 && pos + 1 < (int)sizeof(msg.data))
				msg.data[pos++] = ' ';
			int alen = strlen(argv[i]);
			if (pos + alen < (int)sizeof(msg.data) - 1) {
				memcpy(msg.data + pos, argv[i], alen);
				pos += alen;
			}
		}
		msg.data[pos] = '\0';
		msg.data_size = (pos > 0) ? (pos + 1) : 0;

		if (ioctl(bfd, BINDER_IOC_TRANSACT, &msg) < 0) {
			fprintf(stderr, "service: transaction failed on %s (handle=%d)\n",
				argv[2], sinfo.handle);
			close(bfd);
			return 1;
		}

		print_parcel_dump(&msg);
		close(bfd);
		return 0;
	}

	usage();
	close(bfd);
	return 1;
}
