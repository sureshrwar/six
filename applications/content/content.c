/*
 * applications/content/content.c
 *
 * Android ContentProvider CLI (/bin/content) for SIX.
 *
 * Communicates with MediaProvider ("media.provider" [android.content.IMediaProvider])
 * over /dev/binder:
 *   content query --uri content://media/<vol>/<collection> [--where "expr"]
 *   content insert --uri content://media/<vol>/<collection> --bind _display_name:s:<name> [--bind relative_path:s:<dir>]
 *   content delete --uri content://media/<vol>/<collection> [--where "expr"]
 *   content read --uri content://media/<vol>/<collection>/<id>
 *   content scan
 *   content status | volumes
 *   content mount <uuid> [/mnt/media_rw/<uuid>]
 *   content unmount [<uuid>|ALL]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/binder.h>

#define IMP_QUERY		1
#define IMP_INSERT		2
#define IMP_DELETE		3
#define IMP_SCAN		4
#define IMP_STATUS		5
#define IMP_MOUNT_VOLUME	10
#define IMP_UNMOUNT_VOLUME	11

static void usage(void)
{
	printf("Usage: content <subcommand> [options]\n");
	printf("Subcommands:\n");
	printf("  content query --uri <content://...> [--projection <cols>] [--where \"<expr>\"]\n");
	printf("  content insert --uri <content://...> --bind _display_name:s:<name> [--bind mime_type:s:<mime>]\n");
	printf("  content delete --uri <content://...> [--where \"<expr>\"]\n");
	printf("  content rename --uri <content://com.android.externalstorage.documents/document/...> --bind _display_name:s:<name>\n");
	printf("  content read --uri <content://...>\n");
	printf("  content roots                           List ExternalStorageProvider SAF roots\n");
	printf("  content scan                            Rescan all mounted volumes into MediaStore\n");
	printf("  content status | volumes                Show MediaProvider FUSE volumes & stats\n");
	printf("  content mount <UUID> [lower_path]       Mount /mnt/media_rw/<UUID> at /storage/<UUID>\n");
	printf("  content unmount [UUID|ALL]              Unmount /storage/<UUID> FUSE volume\n");
}

static void extract_bind_val(const char *arg, const char *key, char *out, int out_max)
{
	int klen = (int)strlen(key);
	const char *p;
	if (strncmp(arg, key, klen) != 0 || arg[klen] != ':')
		return;
	p = arg + klen + 1;
	/* Skip type prefix such as s:, i:, l: */
	if (p[0] && p[1] == ':')
		p += 2;
	strncpy(out, p, out_max - 1);
	out[out_max - 1] = '\0';
}

static int is_esp_uri(const char *u)
{
	if (!u)
		return 0;
	if (strncmp(u, "content://com.android.externalstorage.documents", 47) == 0)
		return 1;
	return 0;
}

int main(int argc, char **argv)
{
	int bfd, i, use_esp = 0;
	struct binder_service_info sinfo;
	struct binder_ipc_msg msg;
	const char *subcmd;
	const char *uri = "";
	const char *proj = "";
	const char *where = "";
	char bind_name[64] = {0};
	char bind_rel[64] = {0};
	char bind_mime[48] = {0};
	char bind_content[192] = {0};

	if (argc < 2) {
		usage();
		exit(1);
	}

	subcmd = argv[1];
	if (strcmp(subcmd, "-h") == 0 || strcmp(subcmd, "--help") == 0 || strcmp(subcmd, "help") == 0) {
		usage();
		exit(0);
	}

	for (i = 2; i < argc; i++) {
		if (strcmp(argv[i], "--uri") == 0 && i + 1 < argc) {
			uri = argv[++i];
		} else if (strcmp(argv[i], "--projection") == 0 && i + 1 < argc) {
			proj = argv[++i];
		} else if (strcmp(argv[i], "--where") == 0 && i + 1 < argc) {
			where = argv[++i];
		} else if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
			const char *b = argv[++i];
			extract_bind_val(b, "_display_name", bind_name, sizeof(bind_name));
			extract_bind_val(b, "relative_path", bind_rel, sizeof(bind_rel));
			extract_bind_val(b, "mime_type", bind_mime, sizeof(bind_mime));
			extract_bind_val(b, "content", bind_content, sizeof(bind_content));
		} else if (strncmp(argv[i], "content://", 10) == 0 && !uri[0]) {
			uri = argv[i];
		}
	}

	use_esp = is_esp_uri(uri) || (strcmp(subcmd, "roots") == 0) || (strcmp(subcmd, "rename") == 0);

	bfd = open("/dev/binder", O_RDWR);
	if (bfd < 0) {
		printf("content: cannot open /dev/binder (errno=%d)\n", errno);
		exit(1);
	}

	memset(&sinfo, 0, sizeof(sinfo));
	strcpy(sinfo.name, use_esp ? "externalstorage" : "media.provider");
	if (ioctl(bfd, BINDER_IOC_LOOKUP_SVC, &sinfo) < 0 || sinfo.handle <= 0) {
		printf("content: service '%s' is not registered on /dev/binder\n", sinfo.name);
		close(bfd);
		exit(1);
	}

	memset(&msg, 0, sizeof(msg));
	msg.target_handle = sinfo.handle;
	msg.flags = 0;
	strcpy(msg.interface_token,
	       use_esp ? "android.content.IDocumentsProvider" : "android.content.IMediaProvider");

	if (use_esp) {
		if (strcmp(subcmd, "roots") == 0 ||
		    (strcmp(subcmd, "query") == 0 &&
		     strstr(uri, "/root") != NULL && strstr(uri, "/document/") == NULL)) {
			msg.code = IESP_QUERY_ROOTS;
		} else if (strcmp(subcmd, "query") == 0) {
			int ulen = (int)strlen(uri);
			if (ulen >= 9 && strcmp(uri + ulen - 9, "/children") == 0)
				msg.code = IESP_QUERY_CHILD_DOCUMENTS;
			else
				msg.code = IESP_QUERY_DOCUMENT;
			strncpy(msg.data, uri, sizeof(msg.data) - 1);
			msg.data_size = (unsigned int)strlen(msg.data) + 1;
		} else if (strcmp(subcmd, "insert") == 0) {
			if (!uri[0] || !bind_name[0]) {
				printf("content insert: --uri and --bind _display_name:s:<name> required\n");
				close(bfd);
				exit(1);
			}
			msg.code = IESP_CREATE_DOCUMENT;
			snprintf(msg.data, sizeof(msg.data), "%s|%s|%s|%s",
				 uri, bind_mime[0] ? bind_mime : "text/plain", bind_name, bind_content);
			msg.data_size = (unsigned int)strlen(msg.data) + 1;
		} else if (strcmp(subcmd, "delete") == 0) {
			if (!uri[0]) {
				printf("content delete: --uri required\n");
				close(bfd);
				exit(1);
			}
			msg.code = IESP_DELETE_DOCUMENT;
			strncpy(msg.data, uri, sizeof(msg.data) - 1);
			msg.data_size = (unsigned int)strlen(msg.data) + 1;
		} else if (strcmp(subcmd, "rename") == 0) {
			if (!uri[0] || !bind_name[0]) {
				printf("content rename: --uri and --bind _display_name:s:<name> required\n");
				close(bfd);
				exit(1);
			}
			msg.code = IESP_RENAME_DOCUMENT;
			snprintf(msg.data, sizeof(msg.data), "%s|%s||", uri, bind_name);
			msg.data_size = (unsigned int)strlen(msg.data) + 1;
		} else if (strcmp(subcmd, "read") == 0) {
			char *data_tag;
			msg.code = IESP_QUERY_DOCUMENT;
			strncpy(msg.data, uri, sizeof(msg.data) - 1);
			msg.data_size = (unsigned int)strlen(msg.data) + 1;
			if (ioctl(bfd, BINDER_IOC_TRANSACT, &msg) < 0 || msg.status != 0) {
				printf("content read: %s\n", msg.data[0] ? msg.data : "query failed");
				close(bfd);
				exit(1);
			}
			close(bfd);
			data_tag = strstr(msg.data, "_data=");
			if (!data_tag) {
				printf("content read: %s\n", msg.data);
				exit(1);
			}
			{
				char fpath[128];
				int k = 0, fd, n;
				unsigned char buf[512];
				data_tag += 6;
				while (data_tag[k] && data_tag[k] != ',' && data_tag[k] != '\n' &&
				       k < (int)sizeof(fpath) - 1) {
					fpath[k] = data_tag[k];
					k++;
				}
				fpath[k] = '\0';
				fd = open(fpath, O_RDONLY);
				if (fd < 0) {
					printf("content read: cannot open %s (errno=%d)\n", fpath, errno);
					exit(1);
				}
				while ((n = read(fd, buf, sizeof(buf))) > 0)
					write(1, buf, n);
				close(fd);
				exit(0);
			}
		} else {
			usage();
			close(bfd);
			exit(1);
		}
	} else if (strcmp(subcmd, "query") == 0) {
		if (!uri[0])
			uri = "content://media/external/files";
		msg.code = IMP_QUERY;
		snprintf(msg.data, sizeof(msg.data), "%s|%s|%s", uri, proj, where);
		msg.data_size = (unsigned int)strlen(msg.data) + 1;
	} else if (strcmp(subcmd, "insert") == 0) {
		if (!uri[0] || !bind_name[0]) {
			printf("content insert: --uri and --bind _display_name:s:<name> required\n");
			close(bfd);
			exit(1);
		}
		msg.code = IMP_INSERT;
		snprintf(msg.data, sizeof(msg.data), "%s|%s|%s", uri, bind_name, bind_rel);
		msg.data_size = (unsigned int)strlen(msg.data) + 1;
	} else if (strcmp(subcmd, "delete") == 0) {
		if (!uri[0]) {
			printf("content delete: --uri required\n");
			close(bfd);
			exit(1);
		}
		msg.code = IMP_DELETE;
		snprintf(msg.data, sizeof(msg.data), "%s|%s|", uri, where);
		msg.data_size = (unsigned int)strlen(msg.data) + 1;
	} else if (strcmp(subcmd, "read") == 0) {
		char *data_tag;
		if (!uri[0]) {
			printf("content read: --uri required\n");
			close(bfd);
			exit(1);
		}
		msg.code = IMP_QUERY;
		snprintf(msg.data, sizeof(msg.data), "%s||%s", uri, where);
		msg.data_size = (unsigned int)strlen(msg.data) + 1;
		if (ioctl(bfd, BINDER_IOC_TRANSACT, &msg) < 0 || msg.status != 0) {
			printf("content read: query failed\n");
			close(bfd);
			exit(1);
		}
		close(bfd);
		data_tag = strstr(msg.data, "_data=");
		if (!data_tag) {
			printf("content read: %s\n", msg.data);
			exit(1);
		}
		{
			char fpath[128];
			int k = 0, fd, n;
			unsigned char buf[512];
			data_tag += 6;
			while (data_tag[k] && data_tag[k] != ',' && data_tag[k] != '\n' &&
			       k < (int)sizeof(fpath) - 1) {
				fpath[k] = data_tag[k];
				k++;
			}
			fpath[k] = '\0';
			fd = open(fpath, O_RDONLY);
			if (fd < 0) {
				printf("content read: cannot open %s (errno=%d)\n", fpath, errno);
				exit(1);
			}
			while ((n = read(fd, buf, sizeof(buf))) > 0)
				write(1, buf, n);
			close(fd);
			exit(0);
		}
	} else if (strcmp(subcmd, "scan") == 0) {
		msg.code = IMP_SCAN;
	} else if (strcmp(subcmd, "status") == 0 || strcmp(subcmd, "volumes") == 0) {
		msg.code = IMP_STATUS;
	} else if (strcmp(subcmd, "mount") == 0 && argc >= 3) {
		const char *vid = argv[2];
		char lpath[80];
		if (argc >= 4)
			strncpy(lpath, argv[3], sizeof(lpath) - 1);
		else
			snprintf(lpath, sizeof(lpath), "/mnt/media_rw/%s", vid);
		msg.code = IMP_MOUNT_VOLUME;
		snprintf(msg.data, sizeof(msg.data), "%s|%s|", vid, lpath);
		msg.data_size = (unsigned int)strlen(msg.data) + 1;
	} else if (strcmp(subcmd, "unmount") == 0) {
		const char *vid = (argc >= 3) ? argv[2] : "ALL";
		msg.code = IMP_UNMOUNT_VOLUME;
		strncpy(msg.data, vid, sizeof(msg.data) - 1);
		msg.data_size = (unsigned int)strlen(msg.data) + 1;
	} else {
		usage();
		close(bfd);
		exit(1);
	}

	if (ioctl(bfd, BINDER_IOC_TRANSACT, &msg) < 0 && msg.status == 0) {
		printf("content: Binder transaction failed (errno=%d)\n", errno);
		close(bfd);
		exit(1);
	}

	if (msg.data[0])
		printf("%s\n", msg.data);

	close(bfd);
	exit(msg.status == 0 ? 0 : 1);
	return 0;
}
