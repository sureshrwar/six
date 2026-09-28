/*
 * applications/externalstoraged/externalstoraged.c
 *
 * SIX-native Android ExternalStorageProvider Daemon (/bin/externalstoraged).
 *
 * Implements Android's Storage Access Framework (SAF) DocumentsProvider for
 * shared internal and removable storage:
 *   - Binder Service : "externalstorage" [android.content.IDocumentsProvider]
 *   - Authority      : "com.android.externalstorage.documents"
 *   - Roots          :
 *       1. "primary" (docId "primary:", mounted at /storage/emulated/0)
 *       2. "<UUID>"  (docId "<UUID>:", mounted at /storage/<UUID> for each
 *          active removable USB volume mounted by vold/storaged/mediaproviderd)
 *
 * Architecture:
 *   - Runs as a standalone daemon separate from /bin/mediaproviderd.
 *   - Operates on the FUSE-mounted Scoped Storage paths (/storage/emulated/0
 *     and /storage/<UUID>) served by /bin/mediaproviderd, so all SAF document
 *     creations, renames, ownership updates, and deletions transparently flow
 *     through fuse.mediaprovider and are automatically indexed in MediaStore
 *     (/var/db/media.db).
 *   - Enforces Android 11+ Scoped Storage SAF restrictions per Binder caller
 *     UID (msg->sender_euid), blocking non-root SAF listing of top-level
 *     Android/data and Android/obb directories as well as cross-user access to
 *     private app sandboxes or another user's files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <pwd.h>
#include <sys/stat.h>
#include <asm/statfs.h>
#include <linux/binder.h>

extern int lstat(const char *path, struct stat *buf);
extern int statfs(const char *path, struct statfs *buf);

#define MAX_ESP_ROOTS	4

struct esp_root {
	int	active;
	int	is_usb;
	char	root_id[32];	/* "primary" or "<UUID>" */
	char	title[48];	/* "Internal (/sdcard)" or "<LABEL> (USB)" */
	char	fstype[16];	/* "fuse", "ext4", "ext2", "ntfs", etc. */
	char	upper_path[80];	/* "/storage/emulated/0" or "/storage/<UUID>" */
	char	lower_path[80];	/* "/data/media/0" or "/mnt/media_rw/<UUID>" */
};

static struct esp_root roots[MAX_ESP_ROOTS];

static unsigned long stat_esp_root_queries = 0;
static unsigned long stat_esp_doc_queries = 0;
static unsigned long stat_esp_created = 0;
static unsigned long stat_esp_deleted = 0;
static unsigned long stat_esp_denials = 0;

static int esp_tolower(int c)
{
	return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

static int str_icase_eq(const char *a, const char *b)
{
	while (*a && *b) {
		if (esp_tolower((unsigned char)*a) != esp_tolower((unsigned char)*b))
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

static void classify_doc_mime(const char *filename, char *mime_out)
{
	if (has_ext(filename, ".jpg") || has_ext(filename, ".jpeg"))
		strcpy(mime_out, "image/jpeg");
	else if (has_ext(filename, ".png"))
		strcpy(mime_out, "image/png");
	else if (has_ext(filename, ".gif"))
		strcpy(mime_out, "image/gif");
	else if (has_ext(filename, ".bmp"))
		strcpy(mime_out, "image/bmp");
	else if (has_ext(filename, ".mp3"))
		strcpy(mime_out, "audio/mpeg");
	else if (has_ext(filename, ".wav"))
		strcpy(mime_out, "audio/x-wav");
	else if (has_ext(filename, ".mp4") || has_ext(filename, ".mkv"))
		strcpy(mime_out, "video/mp4");
	else if (has_ext(filename, ".pdf"))
		strcpy(mime_out, "application/pdf");
	else
		strcpy(mime_out, "text/plain");
}

static const char *username_for_uid(uid_t uid, char *buf, int buflen)
{
	struct passwd *pw = getpwuid(uid);
	if (pw && pw->pw_name[0]) {
		strncpy(buf, pw->pw_name, buflen - 1);
		buf[buflen - 1] = '\0';
		return buf;
	}
	snprintf(buf, buflen, "uid_%u", (unsigned int)uid);
	return buf;
}

static int uid_for_username(const char *name)
{
	struct passwd *pw;
	if (!name || !name[0])
		return -1;
	if (isdigit((unsigned char)name[0]))
		return atoi(name);
	pw = getpwnam(name);
	if (pw)
		return (int)pw->pw_uid;
	return -1;
}

static void build_full_path(const char *base_path, const char *rel_path,
			    char *out, int out_max)
{
	if (!rel_path || !rel_path[0])
		snprintf(out, out_max, "%s", base_path);
	else
		snprintf(out, out_max, "%s/%s", base_path, rel_path);
}

static void build_child_rel(const char *parent_rel, const char *name,
			    char *out, int out_max)
{
	if (!parent_rel)
		parent_rel = "";
	if (!name || !name[0] || strcmp(name, ".") == 0) {
		snprintf(out, out_max, "%s", parent_rel);
		return;
	}
	if (strcmp(name, "..") == 0) {
		const char *slash = strrchr(parent_rel, '/');
		if (!slash) {
			out[0] = '\0';
		} else {
			int plen = (int)(slash - parent_rel);
			if (plen >= out_max)
				plen = out_max - 1;
			memcpy(out, parent_rel, plen);
			out[plen] = '\0';
		}
		return;
	}
	if (!parent_rel[0])
		snprintf(out, out_max, "%s", name);
	else
		snprintf(out, out_max, "%s/%s", parent_rel, name);
}

/*
 * Scoped Storage Access Policy Enforcement for ExternalStorageProvider:
 * Returns 0 if allowed, -EACCES if blocked by Scoped Storage.
 */
static int check_scoped_access(const char *rel_path, uid_t caller_uid,
			       int is_write, int target_exists,
			       uid_t target_owner_uid)
{
	/* 1. Root (0) and AID_MEDIA_RW (1023) have full access */
	if (caller_uid == 0 || caller_uid == 1023)
		return 0;

	/* 2. Private per-user app sandboxes: Android/data/<user> and Android/obb/<user> */
	if (strncmp(rel_path, "Android/data/", 13) == 0 ||
	    strncmp(rel_path, "Android/obb/", 12) == 0) {
		const char *p = (rel_path[8] == 'd') ? (rel_path + 13) : (rel_path + 12);
		char sandbox_user[32];
		int idx = 0, sandbox_uid;

		while (p[idx] && p[idx] != '/' && idx < (int)sizeof(sandbox_user) - 1) {
			sandbox_user[idx] = p[idx];
			idx++;
		}
		sandbox_user[idx] = '\0';

		if (sandbox_user[0]) {
			sandbox_uid = uid_for_username(sandbox_user);
			if (sandbox_uid >= 0 && (int)caller_uid != sandbox_uid)
				return -EACCES;
		}
		return 0;
	}

	/* 3. Personal document dirs (Documents/, Download/): non-root owner isolation */
	if (strncmp(rel_path, "Documents/", 10) == 0 ||
	    strncmp(rel_path, "Download/", 9) == 0) {
		if (target_exists && target_owner_uid != 0 && target_owner_uid != caller_uid)
			return -EACCES;
	}

	/* 4. Existing file modification/deletion: only owner (or root) may modify/unlink */
	if (is_write && target_exists && rel_path[0] != '\0') {
		if (target_owner_uid != caller_uid)
			return -EACCES;
	}

	return 0;
}

static int compute_used_pct(const char *upper_path, const char *lower_path)
{
	struct statfs sfs;
	if ((statfs(upper_path, &sfs) == 0 || statfs(lower_path, &sfs) == 0) &&
	    sfs.f_blocks > 0) {
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
 * Discover active storage roots on the system:
 *   - Slot 0: "primary" (/storage/emulated/0 backed by /data/media/0)
 *   - Slots 1..N: Any active removable USB volumes mounted under /storage/<UUID>
 */
static int discover_roots(int bfd)
{
	struct binder_uevent_msg uev;
	DIR *dp;
	struct dirent *de;
	int count = 0;

	memset(roots, 0, sizeof(roots));
	memset(&uev, 0, sizeof(uev));
	ioctl(bfd, BINDER_IOC_USB_STATUS, &uev);

	/* 1. Primary shared storage root */
	roots[0].active = 1;
	roots[0].is_usb = 0;
	strcpy(roots[0].root_id, "primary");
	strcpy(roots[0].title, "Internal (/sdcard)");
	strcpy(roots[0].fstype, "fuse");
	strcpy(roots[0].upper_path, "/storage/emulated/0");
	strcpy(roots[0].lower_path, "/data/media/0");
	count = 1;

	/* 2. Removable USB storage roots mounted at /storage/<UUID> */
	dp = opendir("/storage");
	if (dp) {
		while ((de = readdir(dp)) != NULL && count < MAX_ESP_ROOTS) {
			char upath[80], lpath[80];
			struct stat st, lst;

			if (strcmp(de->d_name, ".") == 0 ||
			    strcmp(de->d_name, "..") == 0 ||
			    strcmp(de->d_name, "emulated") == 0 ||
			    strcmp(de->d_name, "self") == 0 ||
			    strcmp(de->d_name, "usb") == 0)
				continue;

			snprintf(upath, sizeof(upath), "/storage/%s", de->d_name);
			snprintf(lpath, sizeof(lpath), "/mnt/media_rw/%s", de->d_name);
			if (lstat(upath, &st) < 0 || !S_ISDIR(st.st_mode))
				continue;
			if (lstat(lpath, &lst) < 0 || !S_ISDIR(lst.st_mode))
				continue;

			roots[count].active = 1;
			roots[count].is_usb = 1;
			strncpy(roots[count].root_id, de->d_name, sizeof(roots[count].root_id) - 1);
			snprintf(roots[count].title, sizeof(roots[count].title), "%.14s (USB)",
				 uev.label[0] ? uev.label : de->d_name);
			strncpy(roots[count].fstype,
				uev.fstype[0] ? uev.fstype : "ext4",
				sizeof(roots[count].fstype) - 1);
			strncpy(roots[count].upper_path, upath, sizeof(roots[count].upper_path) - 1);
			strncpy(roots[count].lower_path, lpath, sizeof(roots[count].lower_path) - 1);
			count++;
		}
		closedir(dp);
	}

	return count;
}

static void parse_pipe4(const char *src,
			char *a, int amax,
			char *b, int bmax,
			char *c, int cmax,
			char *d, int dmax)
{
	const char *p = src ? src : "";
	const char *sep;
	int len;

	a[0] = '\0';
	b[0] = '\0';
	c[0] = '\0';
	d[0] = '\0';

	sep = strchr(p, '|');
	if (!sep) {
		strncpy(a, p, amax - 1);
		a[amax - 1] = '\0';
		return;
	}
	len = (int)(sep - p);
	if (len >= amax)
		len = amax - 1;
	memcpy(a, p, len);
	a[len] = '\0';
	p = sep + 1;

	sep = strchr(p, '|');
	if (!sep) {
		strncpy(b, p, bmax - 1);
		b[bmax - 1] = '\0';
		return;
	}
	len = (int)(sep - p);
	if (len >= bmax)
		len = bmax - 1;
	memcpy(b, p, len);
	b[len] = '\0';
	p = sep + 1;

	sep = strchr(p, '|');
	if (!sep) {
		strncpy(c, p, cmax - 1);
		c[cmax - 1] = '\0';
		return;
	}
	len = (int)(sep - p);
	if (len >= cmax)
		len = cmax - 1;
	memcpy(c, p, len);
	c[len] = '\0';
	p = sep + 1;

	strncpy(d, p, dmax - 1);
	d[dmax - 1] = '\0';
}

/*
 * Parse an ExternalStorageProvider Document ID or URI:
 *   - "primary:" or "primary:Pictures/note.txt"
 *   - "7B9E-3D10:" or "XYZ:Pictures/six_note.txt"
 *   - "content://com.android.externalstorage.documents/document/primary%3APictures/children"
 */
static int parse_esp_doc_id(int bfd, const char *raw_in,
			    struct esp_root **out_root,
			    char *out_root_id, int root_max,
			    char *out_rel, int rel_max)
{
	char decoded[256];
	char root_part[64];
	const char *p = raw_in ? raw_in : "";
	const char *colon;
	int i = 0, dlen;

	discover_roots(bfd);

	if (out_root)
		*out_root = NULL;
	out_root_id[0] = '\0';
	out_rel[0] = '\0';

	if (strncmp(p, "content://com.android.externalstorage.documents/", 48) == 0) {
		p += 48;
		if (strncmp(p, "document/", 9) == 0)
			p += 9;
		else if (strncmp(p, "tree/", 5) == 0)
			p += 5;
		else if (strncmp(p, "root/", 5) == 0)
			p += 5;
	}

	while (*p && i < (int)sizeof(decoded) - 1) {
		if (p[0] == '%' && p[1] == '3' && (p[2] == 'A' || p[2] == 'a')) {
			decoded[i++] = ':';
			p += 3;
		} else if (p[0] == '%' && p[1] == '2' && (p[2] == 'F' || p[2] == 'f')) {
			decoded[i++] = '/';
			p += 3;
		} else {
			decoded[i++] = *p++;
		}
	}
	decoded[i] = '\0';

	dlen = (int)strlen(decoded);
	if (dlen >= 9 && strcmp(decoded + dlen - 9, "/children") == 0)
		decoded[dlen - 9] = '\0';

	colon = strchr(decoded, ':');
	if (colon) {
		int rlen = (int)(colon - decoded);
		if (rlen >= (int)sizeof(root_part))
			rlen = sizeof(root_part) - 1;
		memcpy(root_part, decoded, rlen);
		root_part[rlen] = '\0';
		p = colon + 1;
		while (*p == '/')
			p++;
		strncpy(out_rel, p, rel_max - 1);
		out_rel[rel_max - 1] = '\0';
	} else {
		strncpy(root_part, decoded, sizeof(root_part) - 1);
		root_part[sizeof(root_part) - 1] = '\0';
		out_rel[0] = '\0';
	}

	dlen = (int)strlen(out_rel);
	while (dlen > 0 && out_rel[dlen - 1] == '/')
		out_rel[--dlen] = '\0';

	if (!root_part[0] ||
	    str_icase_eq(root_part, "primary") ||
	    str_icase_eq(root_part, "external_primary")) {
		if (!roots[0].active)
			return -ENOENT;
		if (out_root)
			*out_root = &roots[0];
		strncpy(out_root_id, "primary", root_max - 1);
		out_root_id[root_max - 1] = '\0';
		return 0;
	}

	for (i = 0; i < MAX_ESP_ROOTS; i++) {
		if (roots[i].active && str_icase_eq(roots[i].root_id, root_part)) {
			if (out_root)
				*out_root = &roots[i];
			strncpy(out_root_id, roots[i].root_id, root_max - 1);
			out_root_id[root_max - 1] = '\0';
			return 0;
		}
	}

	return -ENOENT;
}

static void handle_esp_binder_request(int bfd, struct binder_ipc_msg *msg)
{
	struct binder_ipc_msg reply;

	memset(&reply, 0, sizeof(reply));
	reply.txn_id = msg->txn_id;
	reply.status = 0;

	switch (msg->code) {
	case DUMP_TRANSACTION: {
		int i, root_cnt = discover_roots(bfd), pos = 0;

		pos += snprintf(reply.data + pos, sizeof(reply.data) - pos,
				"ExternalStorageProvider (dumpsys externalstorage)\n"
				"  Authority: com.android.externalstorage.documents (pid=%d)\n"
				"  Active SAF Roots: %d\n"
				"  Stats: root_queries=%lu doc_queries=%lu created=%lu deleted=%lu saf_denials=%lu",
				getpid(), root_cnt,
				stat_esp_root_queries, stat_esp_doc_queries,
				stat_esp_created, stat_esp_deleted, stat_esp_denials);
		for (i = 0; i < MAX_ESP_ROOTS && pos < (int)sizeof(reply.data) - 96; i++) {
			if (!roots[i].active)
				continue;
			pos += snprintf(reply.data + pos, sizeof(reply.data) - pos,
					"\n  Root [%d]: %s (docId=%s: mount=%s)",
					i, roots[i].root_id, roots[i].root_id,
					roots[i].upper_path);
		}
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	case PING_TRANSACTION:
		snprintf(reply.data, sizeof(reply.data),
			 "PONG from ExternalStorageProvider (com.android.externalstorage.documents, pid=%d)",
			 getpid());
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;

	case IESP_QUERY_ROOTS: {
		int i, row_idx = 0, pos = 0;

		stat_esp_root_queries++;
		discover_roots(bfd);

		for (i = 0; i < MAX_ESP_ROOTS; i++) {
			struct esp_root *r = &roots[i];
			int pct;

			if (!r->active)
				continue;
			pct = compute_used_pct(r->upper_path, r->lower_path);
			if (pos > 0 && pos < (int)sizeof(reply.data) - 2)
				reply.data[pos++] = '\n';
			pos += snprintf(reply.data + pos, sizeof(reply.data) - pos,
					"Row: %d root_id=%s, document_id=%s:, title=%s, mount_path=%s, fstype=%s, used_pct=%d, flags=0x17",
					row_idx++, r->root_id, r->root_id,
					r->title, r->upper_path, r->fstype, pct);
		}
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	case IESP_QUERY_DOCUMENT: {
		struct esp_root *root = NULL;
		char root_id[32], rel_path[160], upper_full[256];
		char mime[32], owner_name[24];
		const char *disp_name;
		struct stat st;
		uid_t owner_uid;
		int is_d;

		stat_esp_doc_queries++;
		if (parse_esp_doc_id(bfd, msg->data, &root, root_id, sizeof(root_id),
				     rel_path, sizeof(rel_path)) < 0 || !root) {
			reply.status = -ENOENT;
			strcpy(reply.data, "Document root not found");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		build_full_path(root->upper_path, rel_path, upper_full, sizeof(upper_full));
		if (lstat(upper_full, &st) < 0) {
			reply.status = -ENOENT;
			strcpy(reply.data, "Document not found");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		owner_uid = st.st_uid;
		if (check_scoped_access(rel_path, (uid_t)msg->sender_euid, 0, 1, owner_uid) < 0) {
			stat_esp_denials++;
			reply.status = -EACCES;
			snprintf(reply.data, sizeof(reply.data),
				 "SecurityException: Scoped Storage denied access to %s:%s",
				 root_id, rel_path);
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		is_d = S_ISDIR(st.st_mode) ? 1 : 0;
		if (is_d)
			strcpy(mime, "vnd.android.document/directory");
		else
			classify_doc_mime(rel_path, mime);

		if (!rel_path[0]) {
			disp_name = root_id;
		} else {
			const char *sl = strrchr(rel_path, '/');
			disp_name = sl ? (sl + 1) : rel_path;
		}
		username_for_uid(owner_uid, owner_name, sizeof(owner_name));

		snprintf(reply.data, sizeof(reply.data),
			 "Row: 0 document_id=%s:%s, _display_name=%s, mime=%s, _size=%lu, _data=%s, owner=%s(%u), flags=0x%x",
			 root_id, rel_path, disp_name, mime,
			 is_d ? 0UL : (unsigned long)st.st_size,
			 upper_full, owner_name, (unsigned int)owner_uid,
			 is_d ? 0xce : 0x46);
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	case IESP_QUERY_CHILD_DOCUMENTS: {
		struct esp_root *root = NULL;
		char root_id[32], rel_path[160], upper_dir[256];
		DIR *dp;
		struct dirent *de;
		int pass, row_idx = 0, pos = 0;

		stat_esp_doc_queries++;
		if (parse_esp_doc_id(bfd, msg->data, &root, root_id, sizeof(root_id),
				     rel_path, sizeof(rel_path)) < 0 || !root) {
			reply.status = -ENOENT;
			strcpy(reply.data, "Document root not found");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		/*
		 * Android 11+ Scoped Storage SAF enforcement:
		 * Non-root users cannot list the top-level Android/data or Android/obb
		 * tree or another user's private sandbox via ExternalStorageProvider.
		 */
		if (msg->sender_euid != 0 && msg->sender_euid != 1023) {
			if (strcmp(rel_path, "Android/data") == 0 ||
			    strcmp(rel_path, "Android/obb") == 0 ||
			    check_scoped_access(rel_path, (uid_t)msg->sender_euid, 0, 1, 0) < 0) {
				stat_esp_denials++;
				reply.status = -EACCES;
				snprintf(reply.data, sizeof(reply.data),
					 "SecurityException: SAF access to %s:%s is restricted by ExternalStorageProvider",
					 root_id, rel_path);
				reply.data_size = (unsigned int)strlen(reply.data) + 1;
				break;
			}
		}

		build_full_path(root->upper_path, rel_path, upper_dir, sizeof(upper_dir));
		for (pass = 0; pass < 2; pass++) {
			dp = opendir(upper_dir);
			if (!dp)
				break;
			while ((de = readdir(dp)) != NULL) {
				char child_rel[192], child_upper[256];
				char mime[32], owner_name[24];
				struct stat st;
				uid_t owner_uid;
				int is_d;

				if (strcmp(de->d_name, ".") == 0 ||
				    strcmp(de->d_name, "..") == 0 ||
				    strcmp(de->d_name, "lost+found") == 0)
					continue;

				build_child_rel(rel_path, de->d_name, child_rel, sizeof(child_rel));
				build_full_path(root->upper_path, child_rel, child_upper, sizeof(child_upper));
				if (lstat(child_upper, &st) < 0)
					continue;

				is_d = S_ISDIR(st.st_mode) ? 1 : 0;
				if ((pass == 0 && !is_d) || (pass == 1 && is_d))
					continue;

				owner_uid = st.st_uid;
				username_for_uid(owner_uid, owner_name, sizeof(owner_name));

				if (is_d)
					strcpy(mime, "vnd.android.document/directory");
				else
					classify_doc_mime(de->d_name, mime);

				if (pos > 0 && pos < (int)sizeof(reply.data) - 2)
					reply.data[pos++] = '\n';
				pos += snprintf(reply.data + pos, sizeof(reply.data) - pos,
						"Row: %d document_id=%s:%s, _display_name=%s, mime=%s, _size=%lu, _data=%s, owner=%s(%u)",
						row_idx++, root_id, child_rel, de->d_name, mime,
						is_d ? 0UL : (unsigned long)st.st_size,
						child_upper, owner_name, (unsigned int)owner_uid);
				if (pos >= (int)sizeof(reply.data) - 110)
					break;
			}
			closedir(dp);
		}

		if (row_idx == 0)
			strcpy(reply.data, "No result found.");
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	case IESP_CREATE_DOCUMENT: {
		/* msg->data = "<parentDocId>|<mimeType>|<displayName>|<initialContent>" */
		char parent_doc[128], mime[48], dname[64], init_body[256];
		char root_id[32], parent_rel[160], child_rel[192], upper_full[256];
		struct esp_root *root = NULL;

		parse_pipe4(msg->data, parent_doc, sizeof(parent_doc),
			    mime, sizeof(mime), dname, sizeof(dname),
			    init_body, sizeof(init_body));
		if (!dname[0] ||
		    parse_esp_doc_id(bfd, parent_doc, &root, root_id, sizeof(root_id),
				     parent_rel, sizeof(parent_rel)) < 0 || !root) {
			reply.status = -EINVAL;
			strcpy(reply.data, "Invalid createDocument arguments");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		build_child_rel(parent_rel, dname, child_rel, sizeof(child_rel));
		if (check_scoped_access(child_rel, (uid_t)msg->sender_euid, 1, 0,
					(uid_t)msg->sender_euid) < 0) {
			stat_esp_denials++;
			reply.status = -EACCES;
			snprintf(reply.data, sizeof(reply.data),
				 "SecurityException: Scoped Storage denied createDocument %s:%s",
				 root_id, child_rel);
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		build_full_path(root->upper_path, child_rel, upper_full, sizeof(upper_full));
		if (strcmp(mime, "vnd.android.document/directory") == 0 ||
		    strcmp(mime, "inode/directory") == 0) {
			if (mkdir(upper_full, 0775) < 0) {
				reply.status = -errno;
				snprintf(reply.data, sizeof(reply.data),
					 "mkdir failed for %s:%s", root_id, child_rel);
				reply.data_size = (unsigned int)strlen(reply.data) + 1;
				break;
			}
			chown(upper_full, (uid_t)msg->sender_euid, 0);
		} else {
			int fd = open(upper_full, O_CREAT | O_WRONLY | O_TRUNC, 0644);
			if (fd < 0) {
				reply.status = -errno;
				snprintf(reply.data, sizeof(reply.data),
					 "open(O_CREAT) failed for %s:%s", root_id, child_rel);
				reply.data_size = (unsigned int)strlen(reply.data) + 1;
				break;
			}
			if (init_body[0])
				write(fd, init_body, strlen(init_body));
			close(fd);
			chown(upper_full, (uid_t)msg->sender_euid, 0);
		}

		stat_esp_created++;
		snprintf(reply.data, sizeof(reply.data),
			 "document_id=%s:%s _data=%s",
			 root_id, child_rel, upper_full);
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	case IESP_DELETE_DOCUMENT: {
		struct esp_root *root = NULL;
		char root_id[32], rel_path[160], upper_full[256], owner_name[24];
		struct stat st;
		uid_t owner_uid;
		int rc;

		if (parse_esp_doc_id(bfd, msg->data, &root, root_id, sizeof(root_id),
				     rel_path, sizeof(rel_path)) < 0 || !root || !rel_path[0]) {
			reply.status = -EINVAL;
			strcpy(reply.data, "Invalid documentId for deleteDocument");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		build_full_path(root->upper_path, rel_path, upper_full, sizeof(upper_full));
		if (lstat(upper_full, &st) < 0) {
			reply.status = -ENOENT;
			strcpy(reply.data, "Document does not exist");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		owner_uid = st.st_uid;
		username_for_uid(owner_uid, owner_name, sizeof(owner_name));

		if (check_scoped_access(rel_path, (uid_t)msg->sender_euid, 1, 1, owner_uid) < 0) {
			stat_esp_denials++;
			reply.status = -EACCES;
			snprintf(reply.data, sizeof(reply.data),
				 "SecurityException: uid %d cannot delete %s:%s owned by %s(%u)",
				 msg->sender_euid, root_id, rel_path,
				 owner_name, (unsigned int)owner_uid);
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		if (S_ISDIR(st.st_mode))
			rc = rmdir(upper_full);
		else
			rc = unlink(upper_full);

		if (rc < 0) {
			reply.status = -errno;
			snprintf(reply.data, sizeof(reply.data),
				 "Failed to delete %s:%s (errno=%d)", root_id, rel_path, errno);
		} else {
			stat_esp_deleted++;
			snprintf(reply.data, sizeof(reply.data),
				 "Deleted document %s:%s", root_id, rel_path);
		}
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	case IESP_RENAME_DOCUMENT: {
		char doc_in[128], new_name[64], dummy1[16], dummy2[16];
		char root_id[32], rel_path[160], parent_rel[160], new_rel[192];
		char old_upper[256], new_upper[256];
		struct esp_root *root = NULL;
		struct stat st;
		uid_t owner_uid;
		char *slash;

		parse_pipe4(msg->data, doc_in, sizeof(doc_in),
			    new_name, sizeof(new_name),
			    dummy1, sizeof(dummy1), dummy2, sizeof(dummy2));
		if (!new_name[0] ||
		    parse_esp_doc_id(bfd, doc_in, &root, root_id, sizeof(root_id),
				     rel_path, sizeof(rel_path)) < 0 || !root || !rel_path[0]) {
			reply.status = -EINVAL;
			strcpy(reply.data, "Invalid renameDocument arguments");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		build_full_path(root->upper_path, rel_path, old_upper, sizeof(old_upper));
		if (lstat(old_upper, &st) < 0) {
			reply.status = -ENOENT;
			strcpy(reply.data, "Document does not exist");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		owner_uid = st.st_uid;
		if (check_scoped_access(rel_path, (uid_t)msg->sender_euid, 1, 1, owner_uid) < 0) {
			stat_esp_denials++;
			reply.status = -EACCES;
			strcpy(reply.data, "SecurityException: Scoped Storage denied renameDocument");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		strncpy(parent_rel, rel_path, sizeof(parent_rel) - 1);
		parent_rel[sizeof(parent_rel) - 1] = '\0';
		slash = strrchr(parent_rel, '/');
		if (slash)
			*slash = '\0';
		else
			parent_rel[0] = '\0';

		build_child_rel(parent_rel, new_name, new_rel, sizeof(new_rel));
		build_full_path(root->upper_path, new_rel, new_upper, sizeof(new_upper));

		if (rename(old_upper, new_upper) < 0) {
			reply.status = -errno;
			strcpy(reply.data, "rename failed");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		snprintf(reply.data, sizeof(reply.data),
			 "document_id=%s:%s _data=%s",
			 root_id, new_rel, new_upper);
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	case IESP_IS_CHILD_DOCUMENT: {
		char p_doc[128], c_doc[128], d1[16], d2[16];
		char p_root[32], c_root[32], p_rel[160], c_rel[160];
		struct esp_root *r1 = NULL, *r2 = NULL;
		int is_child = 0;

		parse_pipe4(msg->data, p_doc, sizeof(p_doc), c_doc, sizeof(c_doc),
			    d1, sizeof(d1), d2, sizeof(d2));
		if (parse_esp_doc_id(bfd, p_doc, &r1, p_root, sizeof(p_root), p_rel, sizeof(p_rel)) == 0 &&
		    parse_esp_doc_id(bfd, c_doc, &r2, c_root, sizeof(c_root), c_rel, sizeof(c_rel)) == 0 &&
		    r1 && strcmp(p_root, c_root) == 0) {
			int plen = (int)strlen(p_rel);
			if (plen == 0) {
				is_child = 1;
			} else if (strncmp(c_rel, p_rel, plen) == 0 &&
				   (c_rel[plen] == '/' || c_rel[plen] == '\0')) {
				is_child = 1;
			}
		}
		snprintf(reply.data, sizeof(reply.data), "is_child=%s", is_child ? "true" : "false");
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	default:
		snprintf(reply.data, sizeof(reply.data),
			 "IDocumentsProvider[code=%u] handled by externalstoraged", msg->code);
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	if (!(msg->flags & TF_ONE_WAY))
		ioctl(bfd, BINDER_IOC_REPLY, &reply);
}

int main(int argc, char **argv)
{
	int bfd;
	struct binder_service_info svc;

	(void)argc;
	(void)argv;

	bfd = open("/dev/binder", O_RDWR);
	if (bfd < 0) {
		printf("externalstoraged: cannot open /dev/binder (errno=%d)\n", errno);
		return 1;
	}

	memset(&svc, 0, sizeof(svc));
	strcpy(svc.name, "externalstorage");
	strcpy(svc.descriptor, "android.content.IDocumentsProvider");
	if (ioctl(bfd, BINDER_IOC_REGISTER_SVC, &svc) < 0) {
		printf("externalstoraged: failed to register externalstorage service\n");
		close(bfd);
		return 1;
	}

	discover_roots(bfd);
	printf("externalstoraged: ExternalStorageProvider ready (pid=%d, handle=%d, authority=com.android.externalstorage.documents)\n",
	       getpid(), svc.handle);
	fflush(stdout);

	for (;;) {
		struct binder_ipc_msg msg;
		memset(&msg, 0, sizeof(msg));
		if (ioctl(bfd, BINDER_IOC_RECV, &msg) == 0)
			handle_esp_binder_request(bfd, &msg);
	}

	close(bfd);
	return 0;
}
