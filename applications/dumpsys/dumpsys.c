/*
 * dumpsys.c - Android Binder system service diagnostic tool (/bin/dumpsys)
 *
 * Queries /dev/binder (servicemanager) for registered Binder services and
 * dispatches DUMP_TRANSACTION (0x5f444d50 / '_DMP') RPCs to inspect each
 * service's live state.
 *
 * Usage:
 *   dumpsys                  Dump all running Binder services
 *   dumpsys -l               List currently running Binder services
 *   dumpsys SERVICE [ARGS]   Dump a specific Binder service
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/unistd.h>
#include <linux/binder.h>
#include <android/os/IServiceManager.h>
#include <android/os/storage/IStorageManager.h>
#include <android/content/IMediaProvider.h>
#include <android/content/IDocumentsProvider.h>

extern int open(const char *pathname, int flags, ...);
extern int close(int fd);
extern int ioctl(int fd, int request, ...);

static void usage(void)
{
	printf("usage: dumpsys [-l] [-t TIMEOUT] [SERVICE [ARGS...]]\n"
	       "         -l: only list running Binder services, do not dump them\n"
	       "         -t TIMEOUT: timeout in seconds to wait for a service dump\n"
	       "         SERVICE: dump only service SERVICE (e.g. power, mount, vold,\n"
	       "                  media.provider, externalstorage, suspend, sysinfo)\n");
}

static const char *resolve_service_alias(const char *name)
{
	if (strcmp(name, "media") == 0)
		return IMEDIAPROVIDER_SERVICE_NAME;
	if (strcmp(name, "storage") == 0)
		return ISTORAGEMANAGER_SERVICE_NAME;
	if (strcmp(name, "documents") == 0 ||
	    strcmp(name, "externalstorage.documents") == 0 ||
	    strcmp(name, EXTERNALSTORAGE_AUTHORITY) == 0)
		return IDOCUMENTSPROVIDER_SERVICE_NAME;
	return name;
}

static int dump_one_service(int bfd, const struct binder_service_info *sinfo,
			    const char *extra_args, int show_banner)
{
	struct binder_ipc_msg msg;

	if (show_banner) {
		printf("-------------------------------------------------------------------------------\n");
	}
	printf("DUMP OF SERVICE %s ([%s], pid=%d, handle=%d):\n",
	       sinfo->name, sinfo->descriptor, sinfo->owner_pid, sinfo->handle);

	memset(&msg, 0, sizeof(msg));
	msg.target_handle = sinfo->handle;
	msg.code = DUMP_TRANSACTION;
	strncpy(msg.interface_token, sinfo->descriptor, sizeof(msg.interface_token) - 1);
	if (extra_args && extra_args[0]) {
		strncpy(msg.data, extra_args, sizeof(msg.data) - 1);
		msg.data_size = strlen(msg.data) + 1;
	}

	if (ioctl(bfd, BINDER_IOC_TRANSACT, &msg) < 0) {
		printf("  *** Error dumping service '%s': transaction failed\n", sinfo->name);
		return -1;
	}

	if (msg.data[0]) {
		printf("%s\n", msg.data);
	} else {
		printf("  (empty dump from %s, status=%d)\n", sinfo->name, msg.status);
	}
	return 0;
}

int main(int argc, char **argv)
{
	int bfd;
	int list_only = 0;
	const char *target_svc = NULL;
	char extra_args[BINDER_MAX_DATA_SIZE];
	struct binder_service_info svcs[BINDER_MAX_SERVICES];
	int count = 0, i, pos = 0;

	extra_args[0] = '\0';

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 ||
		    strcmp(argv[i], "-?") == 0) {
			usage();
			return 0;
		}
		if (strcmp(argv[i], "-l") == 0) {
			list_only = 1;
			continue;
		}
		if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
			i++;
			continue;
		}
		if (!target_svc) {
			target_svc = argv[i];
		} else {
			int alen = strlen(argv[i]);
			if (pos > 0 && pos + 1 < (int)sizeof(extra_args))
				extra_args[pos++] = ' ';
			if (pos + alen < (int)sizeof(extra_args) - 1) {
				memcpy(extra_args + pos, argv[i], alen);
				pos += alen;
				extra_args[pos] = '\0';
			}
		}
	}

	bfd = open("/dev/binder", O_RDWR, 0);
	if (bfd < 0) {
		fprintf(stderr, "dumpsys: cannot open /dev/binder (is binder initialized?)\n");
		return 1;
	}

	while (count < BINDER_MAX_SERVICES) {
		memset(&svcs[count], 0, sizeof(svcs[count]));
		svcs[count].handle = count;
		if (ioctl(bfd, BINDER_IOC_LIST_SVCS, &svcs[count]) < 0)
			break;
		count++;
	}

	if (list_only) {
		printf("Currently running services:\n");
		for (i = 0; i < count; i++) {
			printf("  %s\n", svcs[i].name);
		}
		close(bfd);
		return 0;
	}

	if (target_svc) {
		const char *resolved = resolve_service_alias(target_svc);
		struct binder_service_info sinfo;

		memset(&sinfo, 0, sizeof(sinfo));
		strncpy(sinfo.name, resolved, sizeof(sinfo.name) - 1);
		if (ioctl(bfd, BINDER_IOC_LOOKUP_SVC, &sinfo) < 0) {
			fprintf(stderr, "Can't find service: %s\n", target_svc);
			close(bfd);
			return 1;
		}
		dump_one_service(bfd, &sinfo, extra_args, 0);
		close(bfd);
		return 0;
	}

	printf("Currently running services:\n");
	for (i = 0; i < count; i++) {
		printf("  %s\n", svcs[i].name);
	}
	for (i = 0; i < count; i++) {
		dump_one_service(bfd, &svcs[i], NULL, 1);
	}

	close(bfd);
	return 0;
}
