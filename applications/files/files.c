/*
 * applications/files/files.c
 *
 * Android Desktop "Files" (DocumentsUI) Curses File & Media Manager for SIX.
 *
 * Features:
 *   - 3-Pane Android Desktop layout (80x24):
 *       1. Left Navigation Drawer: MediaProvider Libraries (Recent, Images,
 *          Audio, Videos, Documents) + Storage Devices (Internal /sdcard +
 *          dynamically hotplugged USB volumes /storage/<UUID>).
 *       2. Center File / Media List Pane: Directory browser & ContentProvider
 *          virtual collection viewer with full-width reverse-video highlight
 *          selection bars (no '*' or '>' cursor prefixes).
 *       3. Right Inspector & Preview Pane: Live file attributes, Scoped
 *          Storage access badge, EXIF GPS / ID3v1 metadata (with non-owner
 *          GPS redaction), and live file content preview.
 *   - Reactive auto-refresh via select() (250ms tick):
 *       Automatically detects USB hotplug/unplug events ('usbctl plug ...' /
 *       'usbctl unplug') and file changes triggered from a concurrent
 *       'sadb shell' session and refreshes the UI + toast bar without
 *       requiring a keypress.
 */

#include <curses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <linux/binder.h>

extern int select(int nfds, fd_set *readfds, fd_set *writefds,
		  fd_set *exceptfds, struct timeval *timeout);

struct linux_statfs {
	long f_type;
	long f_bsize;
	long f_blocks;
	long f_bfree;
	long f_bavail;
	long f_files;
	long f_ffree;
	long f_fsid[2];
	long f_namelen;
	long f_spare[6];
};
extern int statfs(const char *path, struct linux_statfs *buf);

/* Binder MediaProvider transaction codes */
#define IMP_QUERY          1
#define IMP_INSERT         2
#define IMP_DELETE         3
#define IMP_SCAN           4
#define IMP_STATUS         5
#define IMP_MOUNT_VOLUME   10
#define IMP_UNMOUNT_VOLUME 11

/* Panes */
#define PANE_SIDEBAR 0
#define PANE_FILES   1

/* Sidebar item types */
#define SB_LIB_RECENT   0
#define SB_LIB_IMAGES   1
#define SB_LIB_AUDIO    2
#define SB_LIB_VIDEOS   3
#define SB_LIB_DOCS     4
#define SB_DEV_INTERNAL 5
#define SB_DEV_USB      6

#define MAX_SIDEBAR_ITEMS 8
#define MAX_FILE_ENTRIES  64

struct sidebar_item {
	int id;
	char label[24];
	char sublabel[24];
	char target_path[128];
	char media_uri[64];
	int count_badge;
	int is_storage;
	int used_pct;
	char fstype[12];
};

struct file_entry {
	char name[64];
	char full_path[192];
	char volume_name[24];
	char mime[32];
	char meta_extra[64];
	unsigned long size;
	uid_t owner_uid;
	char owner_name[24];
	int is_dir;
	int is_parent;
	int media_id;
};

static int bfd = -1;
static int mp_handle = -1;
static uid_t my_uid = 0;
static char my_username[32] = "root";

static int focus_pane = PANE_FILES;
static int sb_count = 0;
static int sb_sel = 0;       /* Highlighted index in sidebar */
static int sb_active = 0;    /* Currently active root/category in sidebar */
static struct sidebar_item sb_items[MAX_SIDEBAR_ITEMS];

static int browse_is_virtual = 0;
static char current_dir[192] = "/storage/emulated/0";
static char current_uri[64]  = "";
static char current_title[64] = "Internal Storage (/sdcard)";

static struct file_entry entries[MAX_FILE_ENTRIES];
static int entry_count = 0;
static int file_sel = 0;
static int file_scroll = 0;

/* Live hotplug & directory change tracking */
static unsigned int last_usb_seqnum = 0;
static int last_usb_online = -1;
static char last_usb_uuid[32] = "";
static char last_usb_label[32] = "";
static char last_usb_fstype[16] = "";
static int last_usb_mp_mounted = -1;
static unsigned long last_dir_signature = 0;

static char toast_msg[80] = "Ready. Live USB hotplug monitor active.";
static int modal_open = 0;

static char mp_tolower_ch(char c)
{
	if (c >= 'A' && c <= 'Z')
		return (char)(c + ('a' - 'A'));
	return c;
}

static int str_icase_eq(const char *a, const char *b)
{
	while (*a && *b) {
		if (mp_tolower_ch(*a) != mp_tolower_ch(*b))
			return 0;
		a++;
		b++;
	}
	return (*a == '\0' && *b == '\0');
}

static int has_ext(const char *name, const char *ext)
{
	int nlen = (int)strlen(name);
	int elen = (int)strlen(ext);
	if (nlen < elen)
		return 0;
	return str_icase_eq(name + nlen - elen, ext);
}

static void resolve_uid_name(uid_t uid, char *out, int out_max)
{
	struct passwd *pw;
	if (uid == 0) {
		strncpy(out, "root", out_max - 1);
		out[out_max - 1] = '\0';
		return;
	}
	pw = getpwuid(uid);
	if (pw && pw->pw_name[0]) {
		strncpy(out, pw->pw_name, out_max - 1);
		out[out_max - 1] = '\0';
		return;
	}
	snprintf(out, out_max, "uid%u", (unsigned int)uid);
}

static void ensure_binder(void)
{
	struct binder_service_info sinfo;

	if (bfd < 0)
		bfd = open("/dev/binder", O_RDWR);
	if (bfd < 0)
		return;

	if (mp_handle <= 0) {
		memset(&sinfo, 0, sizeof(sinfo));
		strcpy(sinfo.name, "media.provider");
		if (ioctl(bfd, BINDER_IOC_LOOKUP_SVC, &sinfo) == 0 && sinfo.handle > 0)
			mp_handle = sinfo.handle;
	}
}

static int mp_transact(unsigned int code, const char *in_str,
		       char *out_buf, int out_max)
{
	struct binder_ipc_msg msg;
	int rc;

	if (out_buf && out_max > 0)
		out_buf[0] = '\0';

	ensure_binder();
	if (bfd < 0 || mp_handle <= 0)
		return -1;

	memset(&msg, 0, sizeof(msg));
	msg.target_handle = mp_handle;
	msg.code = code;
	msg.flags = 0;
	strcpy(msg.interface_token, "android.content.IMediaProvider");
	if (in_str) {
		strncpy(msg.data, in_str, BINDER_MAX_DATA_SIZE - 1);
		msg.data_size = (unsigned int)strlen(msg.data) + 1;
	}

	rc = ioctl(bfd, BINDER_IOC_TRANSACT, &msg);
	if (rc < 0)
		return -1;
	if (out_buf && out_max > 0 && msg.data_size > 0) {
		strncpy(out_buf, msg.data, out_max - 1);
		out_buf[out_max - 1] = '\0';
	}
	return msg.status;
}

static int count_query_rows(const char *uri)
{
	char req[128], resp[BINDER_MAX_DATA_SIZE];
	int count = 0;
	const char *p;

	snprintf(req, sizeof(req), "%s||", uri);
	if (mp_transact(IMP_QUERY, req, resp, sizeof(resp)) < 0)
		return 0;
	if (strncmp(resp, "Row: ", 5) != 0)
		return 0;

	p = resp;
	while (*p) {
		if (strncmp(p, "Row: ", 5) == 0)
			count++;
		while (*p && *p != '\n')
			p++;
		if (*p == '\n')
			p++;
	}
	return count;
}

static int compute_fs_used_pct(const char *path)
{
	struct linux_statfs sfs;
	if (statfs(path, &sfs) == 0 && sfs.f_blocks > 0) {
		long used = sfs.f_blocks - sfs.f_bfree;
		int pct = (int)((used * 100L) / sfs.f_blocks);
		if (pct < 1 && used > 0)
			pct = 1;
		if (pct > 100)
			pct = 100;
		return pct;
	}
	return 12;
}

/*
 * Check if a USB FUSE volume (/storage/<uuid>) is currently mounted in /proc/mounts.
 * If mounted, writes the mounted UUID into `mounted_uuid_out`.
 */
static int find_mounted_usb_fuse(const char *hint_uuid, char *mounted_uuid_out, int out_max)
{
	int fd, n;
	char buf[2048];
	const char *p;

	if (mounted_uuid_out && out_max > 0)
		mounted_uuid_out[0] = '\0';

	fd = open("/proc/mounts", O_RDONLY);
	if (fd < 0)
		return 0;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return 0;
	buf[n] = '\0';

	p = buf;
	while ((p = strstr(p, " /storage/")) != NULL) {
		const char *u = p + 10;
		if (strncmp(u, "emulated", 8) != 0) {
			int k = 0;
			while (u[k] && u[k] != ' ' && u[k] != '\t' && u[k] != '\n' && k < out_max - 1) {
				if (mounted_uuid_out)
					mounted_uuid_out[k] = u[k];
				k++;
			}
			if (mounted_uuid_out)
				mounted_uuid_out[k] = '\0';
			return 1;
		}
		p += 10;
	}
	(void)hint_uuid;
	return 0;
}

/*
 * Rebuild the Left Navigation Drawer items (Libraries + Storage Devices).
 */
static void rebuild_sidebar(void)
{
	int prev_id = (sb_sel >= 0 && sb_sel < sb_count) ? sb_items[sb_sel].id : SB_DEV_INTERNAL;
	int prev_active_id = (sb_active >= 0 && sb_active < sb_count) ? sb_items[sb_active].id : SB_DEV_INTERNAL;
	int i;

	sb_count = 0;

	/* 1. Storage Devices first or Libraries first?
	 * Let's put LIBRARIES (Recent, Images, Audio, Videos, Documents) and
	 * STORAGE DEVICES (Internal Storage, plus USB Drive when plugged in).
	 */
	memset(&sb_items[sb_count], 0, sizeof(struct sidebar_item));
	sb_items[sb_count].id = SB_LIB_RECENT;
	strcpy(sb_items[sb_count].label, "Recent");
	strcpy(sb_items[sb_count].media_uri, "content://media/external/file");
	sb_items[sb_count].count_badge = count_query_rows("content://media/external/file");
	sb_count++;

	memset(&sb_items[sb_count], 0, sizeof(struct sidebar_item));
	sb_items[sb_count].id = SB_LIB_IMAGES;
	strcpy(sb_items[sb_count].label, "Images");
	strcpy(sb_items[sb_count].media_uri, "content://media/external/images/media");
	sb_items[sb_count].count_badge = count_query_rows("content://media/external/images/media");
	sb_count++;

	memset(&sb_items[sb_count], 0, sizeof(struct sidebar_item));
	sb_items[sb_count].id = SB_LIB_AUDIO;
	strcpy(sb_items[sb_count].label, "Audio");
	strcpy(sb_items[sb_count].media_uri, "content://media/external/audio/media");
	sb_items[sb_count].count_badge = count_query_rows("content://media/external/audio/media");
	sb_count++;

	memset(&sb_items[sb_count], 0, sizeof(struct sidebar_item));
	sb_items[sb_count].id = SB_LIB_VIDEOS;
	strcpy(sb_items[sb_count].label, "Videos");
	strcpy(sb_items[sb_count].media_uri, "content://media/external/video/media");
	sb_items[sb_count].count_badge = count_query_rows("content://media/external/video/media");
	sb_count++;

	memset(&sb_items[sb_count], 0, sizeof(struct sidebar_item));
	sb_items[sb_count].id = SB_LIB_DOCS;
	strcpy(sb_items[sb_count].label, "Documents");
	strcpy(sb_items[sb_count].media_uri, "content://media/external/documents");
	sb_items[sb_count].count_badge = count_query_rows("content://media/external/documents");
	sb_count++;

	/* Storage Devices */
	memset(&sb_items[sb_count], 0, sizeof(struct sidebar_item));
	sb_items[sb_count].id = SB_DEV_INTERNAL;
	strcpy(sb_items[sb_count].label, "Internal (/sdcard)");
	strcpy(sb_items[sb_count].sublabel, "/storage/emulated/0");
	strcpy(sb_items[sb_count].target_path, "/storage/emulated/0");
	sb_items[sb_count].is_storage = 1;
	sb_items[sb_count].used_pct = compute_fs_used_pct("/storage/emulated/0");
	strcpy(sb_items[sb_count].fstype, "fuse");
	sb_count++;

	if (last_usb_online > 0 && last_usb_uuid[0] && last_usb_mp_mounted > 0) {
		memset(&sb_items[sb_count], 0, sizeof(struct sidebar_item));
		sb_items[sb_count].id = SB_DEV_USB;
		snprintf(sb_items[sb_count].label, sizeof(sb_items[sb_count].label),
			 "%.14s (USB)", last_usb_label[0] ? last_usb_label : last_usb_uuid);
		snprintf(sb_items[sb_count].sublabel, sizeof(sb_items[sb_count].sublabel),
			 "/storage/%s", last_usb_uuid);
		snprintf(sb_items[sb_count].target_path, sizeof(sb_items[sb_count].target_path),
			 "/storage/%s", last_usb_uuid);
		sb_items[sb_count].is_storage = 1;
		sb_items[sb_count].used_pct = compute_fs_used_pct(sb_items[sb_count].target_path);
		strncpy(sb_items[sb_count].fstype, last_usb_fstype, sizeof(sb_items[sb_count].fstype) - 1);
		sb_count++;
	}

	sb_sel = -1;
	sb_active = -1;
	for (i = 0; i < sb_count; i++) {
		if (sb_items[i].id == prev_id)
			sb_sel = i;
		if (sb_items[i].id == prev_active_id)
			sb_active = i;
	}
	if (sb_sel < 0) {
		for (i = 0; i < sb_count; i++) {
			if (sb_items[i].id == SB_DEV_INTERNAL) {
				sb_sel = i;
				break;
			}
		}
		if (sb_sel < 0)
			sb_sel = 0;
	}
	if (sb_active < 0)
		sb_active = sb_sel;
}

/*
 * Helper to extract a value from "key=val," or "key=val\n" in MediaProvider query row.
 */
static void extract_row_field(const char *row, const char *key, char *out, int out_max)
{
	const char *p = strstr(row, key);
	int i = 0;

	out[0] = '\0';
	if (!p)
		return;
	p += strlen(key);
	while (p[i] && p[i] != ',' && p[i] != '\n' && p[i] != '\r' && i < out_max - 1) {
		out[i] = p[i];
		i++;
	}
	out[i] = '\0';
}

static unsigned long compute_dir_signature(const char *dir_path)
{
	DIR *dp;
	struct dirent *de;
	unsigned long sig = 5381UL;

	if (browse_is_virtual)
		return (unsigned long)entry_count;

	dp = opendir(dir_path);
	if (!dp)
		return 0;
	while ((de = readdir(dp)) != NULL) {
		char full[256];
		struct stat st;
		const char *s = de->d_name;
		if (strcmp(s, ".") == 0 || strcmp(s, "..") == 0)
			continue;
		while (*s) {
			sig = ((sig << 5) + sig) + (unsigned char)(*s++);
		}
		snprintf(full, sizeof(full), "%s/%s", dir_path, de->d_name);
		if (lstat(full, &st) == 0) {
			sig ^= (unsigned long)st.st_size * 131UL;
			sig ^= (unsigned long)st.st_mtime * 17UL;
		}
	}
	closedir(dp);
	return sig;
}

static int is_root_storage_dir(const char *path)
{
	if (strcmp(path, "/storage/emulated/0") == 0 ||
	    strcmp(path, "/sdcard") == 0 ||
	    strcmp(path, "/") == 0)
		return 1;
	if (strncmp(path, "/storage/", 9) == 0) {
		const char *rest = path + 9;
		if (strchr(rest, '/') == NULL)
			return 1;
	}
	return 0;
}

/*
 * Load entries for either a VFS directory or a MediaProvider virtual URI.
 */
static void load_entries(void)
{
	entry_count = 0;

	if (browse_is_virtual) {
		char req[128], resp[BINDER_MAX_DATA_SIZE];
		const char *line;

		snprintf(req, sizeof(req), "%s||", current_uri);
		if (mp_transact(IMP_QUERY, req, resp, sizeof(resp)) < 0)
			return;
		if (strncmp(resp, "Row: ", 5) != 0)
			return;

		line = resp;
		while (*line && entry_count < MAX_FILE_ENTRIES) {
			const char *eol = strchr(line, '\n');
			char rowbuf[320];
			int rlen = eol ? (int)(eol - line) : (int)strlen(line);
			if (rlen >= (int)sizeof(rowbuf))
				rlen = sizeof(rowbuf) - 1;
			memcpy(rowbuf, line, rlen);
			rowbuf[rlen] = '\0';

			if (strncmp(rowbuf, "Row: ", 5) == 0) {
				struct file_entry *fe = &entries[entry_count];
				char id_s[16], sz_s[24], own_s[32], lat_s[24], lon_s[24], title_s[32], artist_s[32];
				const char *paren;

				memset(fe, 0, sizeof(*fe));
				extract_row_field(rowbuf, "_id=", id_s, sizeof(id_s));
				extract_row_field(rowbuf, "volume=", fe->volume_name, sizeof(fe->volume_name));
				extract_row_field(rowbuf, "_display_name=", fe->name, sizeof(fe->name));
				extract_row_field(rowbuf, "_data=", fe->full_path, sizeof(fe->full_path));
				extract_row_field(rowbuf, "mime=", fe->mime, sizeof(fe->mime));
				extract_row_field(rowbuf, "_size=", sz_s, sizeof(sz_s));
				extract_row_field(rowbuf, "owner=", own_s, sizeof(own_s));
				extract_row_field(rowbuf, "lat=", lat_s, sizeof(lat_s));
				extract_row_field(rowbuf, "lon=", lon_s, sizeof(lon_s));
				extract_row_field(rowbuf, "title=", title_s, sizeof(title_s));
				extract_row_field(rowbuf, "artist=", artist_s, sizeof(artist_s));

				fe->media_id = atoi(id_s);
				fe->size = (unsigned long)atoi(sz_s);
				fe->is_dir = 0;

				paren = strchr(own_s, '(');
				if (paren) {
					int nlen = (int)(paren - own_s);
					if (nlen >= (int)sizeof(fe->owner_name))
						nlen = sizeof(fe->owner_name) - 1;
					memcpy(fe->owner_name, own_s, nlen);
					fe->owner_name[nlen] = '\0';
					fe->owner_uid = (uid_t)atoi(paren + 1);
				} else {
					strncpy(fe->owner_name, own_s[0] ? own_s : "root", sizeof(fe->owner_name) - 1);
					fe->owner_uid = 0;
				}

				if (lat_s[0] && strcmp(lat_s, "null") != 0) {
					snprintf(fe->meta_extra, sizeof(fe->meta_extra),
						 "GPS: %s, %s", lat_s, lon_s);
				} else if (title_s[0] && strcmp(title_s, "null") != 0) {
					snprintf(fe->meta_extra, sizeof(fe->meta_extra),
						 "%s - %s", title_s, artist_s);
				}
				entry_count++;
			}

			if (!eol)
				break;
			line = eol + 1;
		}
	} else {
		DIR *dp;
		struct dirent *de;
		int pass;

		if (!is_root_storage_dir(current_dir)) {
			struct file_entry *fe = &entries[entry_count++];
			char *slash;
			memset(fe, 0, sizeof(*fe));
			strcpy(fe->name, "..");
			strncpy(fe->full_path, current_dir, sizeof(fe->full_path) - 1);
			slash = strrchr(fe->full_path, '/');
			if (slash && slash != fe->full_path)
				*slash = '\0';
			else
				strcpy(fe->full_path, "/");
			strcpy(fe->mime, "inode/directory");
			strcpy(fe->owner_name, "root");
			fe->is_dir = 1;
			fe->is_parent = 1;
		}

		/* Pass 0: directories first; Pass 1: regular files */
		for (pass = 0; pass < 2; pass++) {
			dp = opendir(current_dir);
			if (!dp)
				break;
			while ((de = readdir(dp)) != NULL && entry_count < MAX_FILE_ENTRIES) {
				char full[256];
				struct stat st;
				int is_d;
				struct file_entry *fe;

				if (strcmp(de->d_name, ".") == 0 ||
				    strcmp(de->d_name, "..") == 0 ||
				    strcmp(de->d_name, "lost+found") == 0)
					continue;

				snprintf(full, sizeof(full), "%s/%s", current_dir, de->d_name);
				if (lstat(full, &st) < 0)
					continue;
				is_d = S_ISDIR(st.st_mode) ? 1 : 0;
				if ((pass == 0 && !is_d) || (pass == 1 && is_d))
					continue;

				fe = &entries[entry_count++];
				memset(fe, 0, sizeof(*fe));
				strncpy(fe->name, de->d_name, sizeof(fe->name) - 1);
				strncpy(fe->full_path, full, sizeof(fe->full_path) - 1);
				fe->size = (unsigned long)st.st_size;
				fe->owner_uid = st.st_uid;
				resolve_uid_name(st.st_uid, fe->owner_name, sizeof(fe->owner_name));
				fe->is_dir = is_d;

				if (is_d) {
					strcpy(fe->mime, "inode/directory");
				} else if (has_ext(fe->name, ".jpg") || has_ext(fe->name, ".jpeg")) {
					strcpy(fe->mime, "image/jpeg");
				} else if (has_ext(fe->name, ".png")) {
					strcpy(fe->mime, "image/png");
				} else if (has_ext(fe->name, ".mp3")) {
					strcpy(fe->mime, "audio/mpeg");
				} else if (has_ext(fe->name, ".mp4")) {
					strcpy(fe->mime, "video/mp4");
				} else {
					strcpy(fe->mime, "text/plain");
				}
			}
			closedir(dp);
		}
		last_dir_signature = compute_dir_signature(current_dir);
	}

	if (file_sel >= entry_count)
		file_sel = entry_count > 0 ? entry_count - 1 : 0;
	if (file_sel < 0)
		file_sel = 0;
	if (file_sel < file_scroll)
		file_scroll = file_sel;
	if (file_sel >= file_scroll + 17)
		file_scroll = file_sel - 16;
}

static void activate_sidebar_item(int idx)
{
	struct sidebar_item *it;
	if (idx < 0 || idx >= sb_count)
		return;
	sb_active = idx;
	it = &sb_items[idx];

	if (it->is_storage) {
		browse_is_virtual = 0;
		strncpy(current_dir, it->target_path, sizeof(current_dir) - 1);
		strncpy(current_title, it->label, sizeof(current_title) - 1);
	} else {
		browse_is_virtual = 1;
		strncpy(current_uri, it->media_uri, sizeof(current_uri) - 1);
		snprintf(current_title, sizeof(current_title), "Library: %s", it->label);
	}
	file_sel = 0;
	file_scroll = 0;
	load_entries();
}

/*
 * Poll USB hotplug state and directory changes.
 * Returns 1 if UI needs a redraw, 0 if unchanged.
 */
static int poll_external_changes(int is_initial)
{
	struct binder_uevent_msg uev;
	int changed = 0;
	int usb_mp_mounted = 0;

	ensure_binder();
	if (bfd >= 0 && ioctl(bfd, BINDER_IOC_USB_STATUS, &uev) == 0) {
		char actual_uuid[32];
		actual_uuid[0] = '\0';
		if (uev.online) {
			usb_mp_mounted = find_mounted_usb_fuse(uev.uuid, actual_uuid, sizeof(actual_uuid));
			if (actual_uuid[0]) {
				strncpy(uev.uuid, actual_uuid, sizeof(uev.uuid) - 1);
				uev.uuid[sizeof(uev.uuid) - 1] = '\0';
			}
		}

		if (is_initial ||
		    uev.online != last_usb_online ||
		    uev.seqnum != last_usb_seqnum ||
		    usb_mp_mounted != last_usb_mp_mounted ||
		    strcmp(uev.uuid, last_usb_uuid) != 0) {
			int was_online = last_usb_online;
			int was_mounted = last_usb_mp_mounted;

			last_usb_online = uev.online;
			last_usb_seqnum = uev.seqnum;
			last_usb_mp_mounted = usb_mp_mounted;
			strncpy(last_usb_uuid, uev.uuid, sizeof(last_usb_uuid) - 1);
			strncpy(last_usb_label, uev.label, sizeof(last_usb_label) - 1);
			strncpy(last_usb_fstype, uev.fstype, sizeof(last_usb_fstype) - 1);

			if (!is_initial) {
				if (uev.online && usb_mp_mounted && (!was_online || !was_mounted)) {
					snprintf(toast_msg, sizeof(toast_msg),
						 "[HOTPLUG] USB Mounted: %s (%s) -> /storage/%s",
						 uev.label[0] ? uev.label : uev.uuid,
						 uev.fstype, uev.uuid);
				} else if (!uev.online && was_online > 0) {
					snprintf(toast_msg, sizeof(toast_msg),
						 "[HOTPLUG] USB Disconnected: volume unmounted");
					if (!browse_is_virtual &&
					    strncmp(current_dir, "/storage/", 9) == 0 &&
					    strncmp(current_dir, "/storage/emulated", 17) != 0) {
						strcpy(current_dir, "/storage/emulated/0");
						strcpy(current_title, "Internal (/sdcard)");
					}
				}
			}

			rebuild_sidebar();
			load_entries();
			changed = 1;
		}
	} else if (is_initial) {
		last_usb_online = 0;
		rebuild_sidebar();
		load_entries();
		changed = 1;
	}

	if (!changed && !browse_is_virtual) {
		unsigned long sig = compute_dir_signature(current_dir);
		if (sig != last_dir_signature) {
			rebuild_sidebar();
			load_entries();
			snprintf(toast_msg, sizeof(toast_msg),
				 "[AUTO-REFRESH] Directory updated: %s (%d items)",
				 current_dir, entry_count);
			changed = 1;
		}
	}

	return changed;
}

static void format_size(unsigned long sz, int is_dir, char *out, int out_max)
{
	if (is_dir) {
		snprintf(out, out_max, "<DIR>");
	} else if (sz >= 1024 * 1024) {
		snprintf(out, out_max, "%luM", sz / (1024 * 1024));
	} else if (sz >= 1024) {
		snprintf(out, out_max, "%luK", sz / 1024);
	} else {
		snprintf(out, out_max, "%luB", sz);
	}
}

/*
 * Draw a horizontal padded string at (y, x) of exact width `w` with `attr`.
 */
static void draw_padded(int y, int x, int w, int attr, const char *text)
{
	char buf[128];
	int len;

	if (w <= 0)
		return;
	if (w >= (int)sizeof(buf))
		w = (int)sizeof(buf) - 1;

	memset(buf, ' ', w);
	buf[w] = '\0';
	if (text) {
		len = (int)strlen(text);
		if (len > w)
			len = w;
		memcpy(buf, text, len);
	}

	attrset(attr);
	mvaddstr(y, x, buf);
	attrset(A_NORMAL);
}

static void draw_usage_bar(int y, int x, int pct, const char *fstype)
{
	char bar[24];
	int filled = (pct * 8) / 100;
	int i;

	if (filled < 1 && pct > 0)
		filled = 1;
	if (filled > 8)
		filled = 8;

	bar[0] = ' ';
	bar[1] = '[';
	for (i = 0; i < 8; i++)
		bar[2 + i] = (i < filled) ? '#' : '.';
	bar[10] = ']';
	snprintf(bar + 11, sizeof(bar) - 11, " %2d%% %.4s", pct, fstype ? fstype : "");
	draw_padded(y, x, 21, A_NORMAL, bar);
}

/*
 * Populate and draw the Right Inspector & Preview Pane (cols 59..79, rows 1..20)
 */
static void draw_inspector_pane(void)
{
	struct file_entry *fe;
	char line[32];
	int r = 2;

	draw_padded(1, 59, 21, A_BOLD | A_UNDERLINE, " INSPECTOR");

	for (r = 2; r <= 20; r++)
		draw_padded(r, 59, 21, A_NORMAL, "");

	if (entry_count <= 0 || file_sel < 0 || file_sel >= entry_count) {
		draw_padded(3, 59, 21, A_NORMAL, " (No item selected)");
		return;
	}

	fe = &entries[file_sel];
	snprintf(line, sizeof(line), " %.19s", fe->name);
	draw_padded(2, 59, 21, A_BOLD, line);

	snprintf(line, sizeof(line), " Type: %.13s", fe->mime);
	draw_padded(3, 59, 21, A_NORMAL, line);

	if (fe->is_dir) {
		draw_padded(4, 59, 21, A_NORMAL, " Size: Directory");
	} else {
		snprintf(line, sizeof(line), " Size: %lu bytes", fe->size);
		draw_padded(4, 59, 21, A_NORMAL, line);
	}

	snprintf(line, sizeof(line), " Owner: %s(%u)", fe->owner_name, (unsigned int)fe->owner_uid);
	draw_padded(5, 59, 21, A_NORMAL, line);

	/* Scoped Storage Access Badge */
	if (my_uid == 0) {
		draw_padded(6, 59, 21, A_BOLD, " Scope: FULL (ROOT)");
	} else if (strstr(fe->full_path, "/Android/data/") != NULL) {
		char my_sbx[64];
		snprintf(my_sbx, sizeof(my_sbx), "/Android/data/%s", my_username);
		if (strstr(fe->full_path, my_sbx) != NULL)
			draw_padded(6, 59, 21, A_BOLD, " Scope: APP SANDBOX");
		else
			draw_padded(6, 59, 21, A_BOLD, " Scope: LOCKED (UID)");
	} else if (fe->owner_uid == my_uid || fe->is_dir) {
		draw_padded(6, 59, 21, A_BOLD, " Scope: RW (OWNER)");
	} else {
		draw_padded(6, 59, 21, A_NORMAL, " Scope: RO (SCOPED)");
	}

	/* Read file header via FUSE to show EXIF / ID3 / text preview */
	if (!fe->is_dir) {
		int fd = open(fe->full_path, O_RDONLY);
		if (fd < 0) {
			draw_padded(8, 59, 21, A_REVERSE, " ACCESS DENIED ");
			draw_padded(9, 59, 21, A_NORMAL, " Blocked by Scoped");
			draw_padded(10, 59, 21, A_NORMAL, " Storage FUSE policy");
		} else {
			unsigned char buf[512];
			int n = read(fd, buf, sizeof(buf) - 1);
			close(fd);
			if (n < 0)
				n = 0;
			buf[n] = '\0';

			if (strcmp(fe->mime, "image/jpeg") == 0) {
				char *exif_p = NULL;
				int i;
				draw_padded(8, 59, 21, A_UNDERLINE, " EXIF Metadata:");
				for (i = 0; i + 5 < n; i++) {
					if (memcmp(buf + i, "EXIF:", 5) == 0) {
						exif_p = (char *)(buf + i + 5);
						break;
					}
				}
				if (exif_p) {
					char *lat = strstr(exif_p, "GPSLatitude=");
					char *lon = strstr(exif_p, "GPSLongitude=");
					draw_padded(9, 59, 21, A_NORMAL, " Model: Pixel-SIX");
					if (lat) {
						char v[16];
						int k = 0;
						lat += 12;
						while (lat[k] && lat[k] != ';' && k < 14) {
							v[k] = lat[k];
							k++;
						}
						v[k] = '\0';
						snprintf(line, sizeof(line), " Lat: %.14s", v);
						draw_padded(10, 59, 21, A_BOLD, line);
					}
					if (lon) {
						char v[16];
						int k = 0;
						lon += 13;
						while (lon[k] && lon[k] != ';' && k < 14) {
							v[k] = lon[k];
							k++;
						}
						v[k] = '\0';
						snprintf(line, sizeof(line), " Lon: %.14s", v);
						draw_padded(11, 59, 21, A_BOLD, line);
					}
					if (my_uid != 0 && my_uid != fe->owner_uid) {
						draw_padded(13, 59, 21, A_REVERSE, " EXIF GPS REDACTED ");
					} else {
						draw_padded(13, 59, 21, A_NORMAL, " GPS: Unredacted");
					}
				}
			} else if (strcmp(fe->mime, "audio/mpeg") == 0 && n >= 160 &&
				   memcmp(buf + 32, "TAG", 3) == 0) {
				draw_padded(8, 59, 21, A_UNDERLINE, " ID3v1 Audio Tags:");
				snprintf(line, sizeof(line), " Title: %.12s", (char *)(buf + 35));
				draw_padded(9, 59, 21, A_NORMAL, line);
				snprintf(line, sizeof(line), " Artist:%.12s", (char *)(buf + 65));
				draw_padded(10, 59, 21, A_NORMAL, line);
				snprintf(line, sizeof(line), " Album: %.12s", (char *)(buf + 95));
				draw_padded(11, 59, 21, A_NORMAL, line);
			} else {
				int pr = 9, i = 0;
				draw_padded(8, 59, 21, A_UNDERLINE, " Content Preview:");
				while (i < n && pr <= 18) {
					char pline[22];
					int col = 0;
					plink:
					while (i < n && buf[i] != '\n' && col < 19) {
						unsigned char c = buf[i++];
						if (c >= 32 && c < 127)
							pline[col++] = (char)c;
					}
					while (i < n && buf[i] != '\n')
						i++;
					if (i < n && buf[i] == '\n')
						i++;
					pline[col] = '\0';
					snprintf(line, sizeof(line), " %s", pline);
					draw_padded(pr++, 59, 21, A_NORMAL, line);
					(void)&&plink;
				}
			}
		}
	} else {
		DIR *dp = opendir(fe->full_path);
		if (!dp && errno == EACCES) {
			draw_padded(8, 59, 21, A_REVERSE, " SANDBOX LOCKED ");
			draw_padded(9, 59, 21, A_NORMAL, " Other user's private");
			draw_padded(10, 59, 21, A_NORMAL, " Android/data folder");
		} else if (dp) {
			int sub_cnt = 0;
			struct dirent *de;
			while ((de = readdir(dp)) != NULL) {
				if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0)
					sub_cnt++;
			}
			closedir(dp);
			draw_padded(8, 59, 21, A_UNDERLINE, " Folder Summary:");
			snprintf(line, sizeof(line), " Items: %d", sub_cnt);
			draw_padded(9, 59, 21, A_NORMAL, line);
			draw_padded(11, 59, 21, A_NORMAL, " Press Enter to open");
		}
	}
}

/*
 * Render the entire 3-pane Android Desktop Files UI.
 * Uses full-width reverse-video highlight bars for selection (no '*' or '>').
 */
static void draw_ui(void)
{
	char hdr[128];
	int i, r;

	/* Row 0: Top App Bar (Reverse Video) */
	snprintf(hdr, sizeof(hdr),
		 " Files (Android Desktop)  |  %-32.32s  |  %s(%u)",
		 browse_is_virtual ? current_title : current_dir,
		 my_username, (unsigned int)my_uid);
	draw_padded(0, 0, 80, A_REVERSE | A_BOLD, hdr);

	/* Left Navigation Drawer (Cols 0..21) */
	draw_padded(1, 0, 22, (focus_pane == PANE_SIDEBAR) ? (A_BOLD | A_UNDERLINE) : A_UNDERLINE,
		    " LIBRARIES");

	for (r = 2; r <= 20; r++)
		draw_padded(r, 0, 22, A_NORMAL, "");

	r = 2;
	for (i = 0; i < sb_count; i++) {
		struct sidebar_item *it = &sb_items[i];
		char row_txt[32];
		int attr = A_NORMAL;

		if (it->id == SB_DEV_INTERNAL) {
			r++;
			draw_padded(r++, 0, 22,
				    (focus_pane == PANE_SIDEBAR) ? (A_BOLD | A_UNDERLINE) : A_UNDERLINE,
				    " STORAGE DEVICES");
		}

		if (it->is_storage) {
			snprintf(row_txt, sizeof(row_txt), "  %-19.19s", it->label);
		} else {
			snprintf(row_txt, sizeof(row_txt), "  %-14.14s %3d ", it->label, it->count_badge);
		}

		/* Highlighted selection bar without '*' or '>' */
		if (focus_pane == PANE_SIDEBAR && i == sb_sel) {
			attr = A_REVERSE | A_BOLD;
		} else if (i == sb_active) {
			attr = A_REVERSE;
		}

		draw_padded(r++, 0, 21, attr, row_txt);
		if (it->is_storage && r <= 20) {
			draw_usage_bar(r++, 0, it->used_pct, it->fstype);
		}
	}

	/* Vertical dividers at col 22 and col 58 */
	for (r = 1; r <= 20; r++) {
		mvaddch(r, 22, '|');
		mvaddch(r, 58, '|');
	}

	/* Center File / Media List Pane (Cols 23..57) */
	draw_padded(1, 23, 35,
		    (focus_pane == PANE_FILES) ? (A_BOLD | A_UNDERLINE) : A_UNDERLINE,
		    " NAME                 SIZE  OWNER");

	for (i = 0; i < 19; i++) {
		int idx = file_scroll + i;
		int row_y = 2 + i;

		if (idx < entry_count) {
			struct file_entry *fe = &entries[idx];
			char sz_str[12], disp_name[24], row_txt[48];
			int attr = A_NORMAL;

			format_size(fe->size, fe->is_dir, sz_str, sizeof(sz_str));
			if (fe->is_dir && !fe->is_parent)
				snprintf(disp_name, sizeof(disp_name), "%s/", fe->name);
			else
				snprintf(disp_name, sizeof(disp_name), "%s", fe->name);

			snprintf(row_txt, sizeof(row_txt), " %-19.19s %5.5s  %-6.6s",
				 disp_name, sz_str, fe->owner_name);

			if (focus_pane == PANE_FILES && idx == file_sel) {
				attr = A_REVERSE | A_BOLD;
			} else if (focus_pane != PANE_FILES && idx == file_sel) {
				attr = A_REVERSE;
			} else if (fe->is_dir) {
				attr = A_BOLD;
			}
			draw_padded(row_y, 23, 35, attr, row_txt);
		} else {
			draw_padded(row_y, 23, 35, A_NORMAL, "");
		}
	}

	/* Right Inspector & Preview Pane (Cols 59..79) */
	draw_inspector_pane();

	/* Row 21: Live Toast / Hotplug Status Banner */
	draw_padded(21, 0, 79, A_BOLD, toast_msg);

	/* Row 22: Bottom Keybinding Bar */
	draw_padded(22, 0, 79, A_REVERSE,
		    " Tab:Pane Arrows:Select Enter:Open Bksp:Up n:New m:Dir d:Del e:Eject q:Quit");

	/* Park cursor at bottom right and hide it */
	move(22, 78);
	printf("\033[?25l");
	refresh();
}

/*
 * Modal overlay to view full file contents & metadata when Enter is pressed on a file.
 */
static void show_file_modal(struct file_entry *fe)
{
	int fd, n, i, r;
	unsigned char buf[1024];
	char title[64];

	modal_open = 1;
	fd = open(fe->full_path, O_RDONLY);
	if (fd < 0) {
		snprintf(toast_msg, sizeof(toast_msg),
			 "[SCOPED STORAGE] Permission denied opening %s", fe->name);
		modal_open = 0;
		return;
	}
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n < 0)
		n = 0;
	buf[n] = '\0';

	snprintf(title, sizeof(title), " FILE VIEWER: %s (%lu bytes, owner=%s) ",
		 fe->name, fe->size, fe->owner_name);
	draw_padded(3, 8, 64, A_REVERSE | A_BOLD, title);
	for (r = 4; r <= 18; r++) {
		draw_padded(r, 8, 64, A_REVERSE, "");
	}

	r = 5;
	i = 0;
	while (i < n && r <= 16) {
		char line[62];
		int col = 0;
		while (i < n && buf[i] != '\n' && col < 58) {
			unsigned char c = buf[i++];
			if (c >= 32 && c < 127)
				line[col++] = (char)c;
		}
		while (i < n && buf[i] != '\n')
			i++;
		if (i < n && buf[i] == '\n')
			i++;
		line[col] = '\0';
		if (col > 0) {
			char padded[66];
			snprintf(padded, sizeof(padded), "  %s", line);
			draw_padded(r++, 8, 64, A_REVERSE, padded);
		}
	}
	draw_padded(18, 8, 64, A_REVERSE | A_BOLD,
		    " Press Enter, Space, or Esc to close viewer ");
	refresh();

	for (;;) {
		fd_set rfds;
		struct timeval tv;
		unsigned char ch;
		FD_ZERO(&rfds);
		FD_SET(0, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 250000;
		if (select(1, &rfds, NULL, NULL, &tv) > 0) {
			if (read(0, &ch, 1) == 1) {
				if (ch == '\r' || ch == '\n' || ch == ' ' || ch == 27 || ch == 'q')
					break;
			}
		}
	}
	modal_open = 0;
}

/*
 * Prompt for a short string on row 21 (for 'n' New File or 'm' Mkdir).
 */
static int prompt_input(const char *prompt_str, char *out, int out_max)
{
	int len = 0;
	char line[80];

	out[0] = '\0';
	printf("\033[?25h");
	for (;;) {
		unsigned char ch;
		snprintf(line, sizeof(line), " %s%s", prompt_str, out);
		draw_padded(21, 0, 80, A_REVERSE | A_BOLD, line);
		move(21, 1 + (int)strlen(prompt_str) + len);
		refresh();

		if (read(0, &ch, 1) <= 0)
			break;
		if (ch == 27) {
			out[0] = '\0';
			printf("\033[?25l");
			return 0;
		}
		if (ch == '\r' || ch == '\n') {
			printf("\033[?25l");
			return len > 0 ? 1 : 0;
		}
		if (ch == 127 || ch == '\b') {
			if (len > 0)
				out[--len] = '\0';
		} else if (ch >= 32 && ch < 127 && len < out_max - 1) {
			out[len++] = (char)ch;
			out[len] = '\0';
		}
	}
	printf("\033[?25l");
	return 0;
}

static void handle_create_file(void)
{
	char name[48], path[256], content[128];
	int fd;

	if (browse_is_virtual) {
		strcpy(toast_msg, "[FILES] Select a folder in Storage Devices to create files.");
		return;
	}
	if (!prompt_input("New file name: ", name, sizeof(name))) {
		strcpy(toast_msg, "File creation cancelled.");
		return;
	}
	snprintf(path, sizeof(path), "%s/%s", current_dir, name);
	fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd < 0) {
		snprintf(toast_msg, sizeof(toast_msg),
			 "[SCOPED STORAGE] Cannot create %s: %s", name,
			 (errno == EACCES) ? "Permission denied" : "Read-only or error");
		return;
	}
	snprintf(content, sizeof(content), "Created by %s(uid=%u) in SIX Files app.\n",
		 my_username, (unsigned int)my_uid);
	write(fd, content, strlen(content));
	close(fd);
	rebuild_sidebar();
	load_entries();
	snprintf(toast_msg, sizeof(toast_msg), "[FILES] Created %s (owner=%s)", name, my_username);
}

static void handle_create_dir(void)
{
	char name[48], path[256];

	if (browse_is_virtual) {
		strcpy(toast_msg, "[FILES] Select a folder in Storage Devices to create folders.");
		return;
	}
	if (!prompt_input("New folder name: ", name, sizeof(name))) {
		strcpy(toast_msg, "Folder creation cancelled.");
		return;
	}
	snprintf(path, sizeof(path), "%s/%s", current_dir, name);
	if (mkdir(path, 0775) < 0) {
		snprintf(toast_msg, sizeof(toast_msg),
			 "[SCOPED STORAGE] Cannot mkdir %s: Permission denied", name);
		return;
	}
	load_entries();
	snprintf(toast_msg, sizeof(toast_msg), "[FILES] Created folder %s/", name);
}

static void handle_delete_selected(void)
{
	struct file_entry *fe;
	int rc;

	if (entry_count <= 0 || file_sel < 0 || file_sel >= entry_count)
		return;
	fe = &entries[file_sel];
	if (fe->is_parent)
		return;

	if (fe->is_dir)
		rc = rmdir(fe->full_path);
	else
		rc = unlink(fe->full_path);

	if (rc < 0) {
		snprintf(toast_msg, sizeof(toast_msg),
			 "[SCOPED STORAGE] Permission denied: %s is owned by %s(%u)",
			 fe->name, fe->owner_name, (unsigned int)fe->owner_uid);
	} else {
		char deleted_name[64];
		strncpy(deleted_name, fe->name, sizeof(deleted_name) - 1);
		deleted_name[sizeof(deleted_name) - 1] = '\0';
		rebuild_sidebar();
		load_entries();
		snprintf(toast_msg, sizeof(toast_msg), "[FILES] Deleted %s", deleted_name);
	}
}

static void handle_eject_usb(void)
{
	struct binder_uevent_msg uev;
	char resp[128];

	if (last_usb_online <= 0) {
		strcpy(toast_msg, "[USB] No removable USB storage is currently connected.");
		return;
	}

	mp_transact(IMP_UNMOUNT_VOLUME, "ALL", resp, sizeof(resp));
	if (bfd >= 0) {
		memset(&uev, 0, sizeof(uev));
		strcpy(uev.action, "remove");
		strcpy(uev.subsystem, "block");
		strcpy(uev.devpath, "/devices/pci0000:00/usb1/1-1/block/sda/sda1");
		strcpy(uev.devname, "sda1");
		uev.major = 8;
		uev.minor = 1;
		ioctl(bfd, BINDER_IOC_UEVENT_EMIT, &uev);
	}
	usleep(150000);
	poll_external_changes(0);
	strcpy(toast_msg, "[HOTPLUG] Safely ejected USB storage device.");
}

/*
 * Read a key or escape sequence from stdin when ready.
 */
static int read_key(void)
{
	unsigned char ch;
	if (read(0, &ch, 1) <= 0)
		return -1;
	if (ch == 27) {
		fd_set rfds;
		struct timeval tv;
		unsigned char seq[2];

		FD_ZERO(&rfds);
		FD_SET(0, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 40000;
		if (select(1, &rfds, NULL, NULL, &tv) > 0 && read(0, &seq[0], 1) == 1) {
			if (seq[0] == '[' || seq[0] == 'O') {
				if (select(1, &rfds, NULL, NULL, &tv) > 0 && read(0, &seq[1], 1) == 1) {
					if (seq[1] == 'A') return KEY_UP;
					if (seq[1] == 'B') return KEY_DOWN;
					if (seq[1] == 'C') return KEY_RIGHT;
					if (seq[1] == 'D') return KEY_LEFT;
				}
			}
		}
		return 27;
	}
	return (int)ch;
}

static void dump_text_snapshot(void)
{
	int i;
	printf("=== Android Desktop Files (DocumentsUI) Snapshot [user=%s(%u)] ===\n",
	       my_username, (unsigned int)my_uid);
	printf("Sidebar Roots & Libraries:\n");
	for (i = 0; i < sb_count; i++) {
		struct sidebar_item *it = &sb_items[i];
		if (it->is_storage) {
			printf("  [STORAGE] %-18s -> %s (%d%% used, %s)%s\n",
			       it->label, it->target_path, it->used_pct, it->fstype,
			       (i == sb_active) ? " [ACTIVE]" : "");
		} else {
			printf("  [LIBRARY] %-18s (%d items)%s\n",
			       it->label, it->count_badge,
			       (i == sb_active) ? " [ACTIVE]" : "");
		}
	}
	printf("Current View: %s\n", browse_is_virtual ? current_title : current_dir);
	for (i = 0; i < entry_count; i++) {
		struct file_entry *fe = &entries[i];
		printf("  %-22s  %6luB  owner=%s(%u)  mime=%s\n",
		       fe->name, fe->size, fe->owner_name,
		       (unsigned int)fe->owner_uid, fe->mime);
	}
}

int main(int argc, char **argv)
{
	int dump_only = 0;
	int i;

	my_uid = geteuid();
	resolve_uid_name(my_uid, my_username, sizeof(my_username));

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--dump") == 0) {
			dump_only = 1;
		} else if (argv[i][0] == '/') {
			strncpy(current_dir, argv[i], sizeof(current_dir) - 1);
			browse_is_virtual = 0;
		}
	}

	poll_external_changes(1);

	/* Ensure sb_active matches initial current_dir */
	for (i = 0; i < sb_count; i++) {
		if (sb_items[i].is_storage &&
		    strncmp(current_dir, sb_items[i].target_path, strlen(sb_items[i].target_path)) == 0) {
			sb_sel = i;
			sb_active = i;
			break;
		}
	}

	if (dump_only) {
		dump_text_snapshot();
		if (bfd >= 0)
			close(bfd);
		return 0;
	}

	initscr();
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	clear();
	draw_ui();

	for (;;) {
		fd_set rfds;
		struct timeval tv;
		int rc;

		FD_ZERO(&rfds);
		FD_SET(0, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 250000; /* 250ms reactive hotplug & directory poll */

		rc = select(1, &rfds, NULL, NULL, &tv);
		if (rc > 0 && FD_ISSET(0, &rfds)) {
			int k = read_key();
			if (k < 0)
				continue;

			if (k == 'q' || k == 'Q')
				break;

			if (k == '\t') {
				focus_pane = (focus_pane == PANE_SIDEBAR) ? PANE_FILES : PANE_SIDEBAR;
			} else if (k == KEY_LEFT || k == 'h') {
				focus_pane = PANE_SIDEBAR;
			} else if (k == KEY_RIGHT || k == 'l') {
				if (focus_pane == PANE_SIDEBAR) {
					activate_sidebar_item(sb_sel);
					focus_pane = PANE_FILES;
				} else if (entry_count > 0 && entries[file_sel].is_dir) {
					strcpy(current_dir, entries[file_sel].full_path);
					file_sel = 0;
					file_scroll = 0;
					load_entries();
				}
			} else if (k == KEY_UP || k == 'k') {
				if (focus_pane == PANE_SIDEBAR) {
					if (sb_sel > 0)
						sb_sel--;
				} else {
					if (file_sel > 0)
						file_sel--;
					if (file_sel < file_scroll)
						file_scroll = file_sel;
				}
			} else if (k == KEY_DOWN || k == 'j') {
				if (focus_pane == PANE_SIDEBAR) {
					if (sb_sel + 1 < sb_count)
						sb_sel++;
				} else {
					if (file_sel + 1 < entry_count)
						file_sel++;
					if (file_sel >= file_scroll + 17)
						file_scroll = file_sel - 16;
				}
			} else if (k == '\r' || k == '\n') {
				if (focus_pane == PANE_SIDEBAR) {
					activate_sidebar_item(sb_sel);
					focus_pane = PANE_FILES;
				} else if (entry_count > 0) {
					struct file_entry *fe = &entries[file_sel];
					if (fe->is_dir) {
						char next_dir[192];
						strncpy(next_dir, fe->full_path, sizeof(next_dir) - 1);
						next_dir[sizeof(next_dir) - 1] = '\0';
						strcpy(current_dir, next_dir);
						browse_is_virtual = 0;
						file_sel = 0;
						file_scroll = 0;
						load_entries();
					} else {
						show_file_modal(fe);
					}
				}
			} else if (k == 127 || k == '\b' || k == 'u') {
				if (!browse_is_virtual && !is_root_storage_dir(current_dir)) {
					char *slash = strrchr(current_dir, '/');
					if (slash && slash != current_dir)
						*slash = '\0';
					file_sel = 0;
					file_scroll = 0;
					load_entries();
				}
			} else if (k == 'n' || k == 'N') {
				handle_create_file();
			} else if (k == 'm' || k == 'M') {
				handle_create_dir();
			} else if (k == 'd' || k == 'D') {
				handle_delete_selected();
			} else if (k == 'e' || k == 'E') {
				handle_eject_usb();
			} else if (k == 'r' || k == 'R' || k == 12) {
				char resp[128];
				mp_transact(IMP_SCAN, "", resp, sizeof(resp));
				rebuild_sidebar();
				load_entries();
				strncpy(toast_msg, resp[0] ? resp : "[FILES] MediaProvider scan completed.",
					sizeof(toast_msg) - 1);
				clear();
			}

			draw_ui();
		} else {
			if (poll_external_changes(0)) {
				clear();
				draw_ui();
			}
		}
	}

	printf("\033[?25h");
	endwin();
	if (bfd >= 0)
		close(bfd);
	return 0;
}
