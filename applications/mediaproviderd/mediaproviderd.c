/*
 * applications/mediaproviderd/mediaproviderd.c
 *
 * SIX-native Android MediaProvider Daemon (/bin/mediaproviderd).
 *
 * Combines Android's two core MediaProvider components in a single native daemon:
 *   1. FuseDaemon (/dev/fuse -> fuse.mediaprovider):
 *      - Mounts /data/media/0 at /storage/emulated/0 (/sdcard)
 *      - Mounts removable USB volumes (/mnt/media_rw/<UUID>) at /storage/<UUID>
 *        (/storage/usb) when notified by vold / storaged / content.
 *      - Enforces Android Scoped Storage per caller UID (req.in.uid):
 *          * Private sandboxes (/Android/data/<user>, /Android/obb/<user>) are
 *            accessible only to that user's UID (and root).
 *          * Shared media collections (/DCIM, /Pictures, /Music, /Movies) allow
 *            all users to create new media (recording owner_uid = req.in.uid),
 *            forbid non-owners from modifying or deleting another user's files
 *            (-EACCES), and allow cross-user reads with on-the-fly EXIF GPS
 *            location redaction.
 *          * Personal document collections (/Documents, /Download) restrict
 *            non-owner access to files owned by other non-root users (-EACCES).
 *      - Performs on-the-fly EXIF GPS redaction during FUSE_READ when
 *        req.in.uid != 0 && req.in.uid != owner_uid, zeroing both binary JPEG
 *        APP1 TIFF GPS IFD (0x8825) tags and ASCII EXIF GPS coordinate tags
 *        without modifying the backing file on /mnt/media_rw/<UUID> or /data/media/0.
 *   2. MediaStore ContentProvider ("media.provider" [android.content.IMediaProvider]
 *      on /dev/binder, backed by /var/db/media.db):
 *      - Automatically indexes JPEG/PNG/BMP/GIF images, MP3/WAV audio, video,
 *        and document files on mount, FUSE_CREATE/WRITE/RELEASE, FUSE_RENAME,
 *        and FUSE_UNLINK.
 *      - Services `content query/insert/delete/scan/volumes` over /dev/binder,
 *        redacting latitude/longitude for non-owner callers.
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
#include <sys/mount.h>
#include <sys/time.h>
#include <asm/statfs.h>
#include <linux/fuse.h>
#include <linux/binder.h>

extern int select(int n, fd_set *inp, fd_set *outp, fd_set *exp, struct timeval *tvp);
extern int lstat(const char *path, struct stat *buf);
extern int statfs(const char *path, struct statfs *buf);
extern int truncate(const char *path, off_t length);

/* IMediaProvider Binder transaction codes */
#define IMP_QUERY		1
#define IMP_INSERT		2
#define IMP_DELETE		3
#define IMP_SCAN		4
#define IMP_STATUS		5
#define IMP_MOUNT_VOLUME	10
#define IMP_UNMOUNT_VOLUME	11

/* MediaStore media_type constants */
#define MEDIA_TYPE_NONE		0
#define MEDIA_TYPE_IMAGE	1
#define MEDIA_TYPE_AUDIO	2
#define MEDIA_TYPE_VIDEO	3
#define MEDIA_TYPE_DOCUMENT	4

#define MAX_VOLUMES		4
#define MAX_NODES_PER_VOL	96
#define MAX_OPEN_FH		32
#define MAX_MEDIA_ITEMS		128
#define MEDIA_DB_PATH		"/var/db/media.db"

struct mp_node {
	int		in_use;
	fuse_u64	nodeid;
	char		rel_path[128];	/* "" for volume root, "DCIM/foo.jpg" */
	uid_t		owner_uid;
	gid_t		owner_gid;
};

struct mp_volume {
	int		active;
	int		fuse_fd;
	char		vol_id[32];	/* e.g. "external_primary" or "4A8F-9C21" */
	char		vol_name[32];	/* lowercase: "external_primary" or "4a8f-9c21" */
	char		lower_path[80];	/* e.g. "/data/media/0" or "/mnt/media_rw/4A8F-9C21" */
	char		upper_path[80];	/* e.g. "/storage/emulated/0" or "/storage/4A8F-9C21" */
	int		is_usb;
	struct mp_node	nodes[MAX_NODES_PER_VOL];
};

struct mp_open_fh {
	int		in_use;
	int		lower_fd;
	int		vol_idx;
	fuse_u64	nodeid;
	int		was_written;
};

struct media_item {
	int		in_use;
	int		id;
	char		volume_name[24];	/* "external_primary", "4a8f-9c21", etc. */
	char		rel_path[104];		/* "DCIM/Camera/IMG_20260927_GPS.jpg" */
	char		data_path[128];		/* "/storage/emulated/0/DCIM/..." */
	char		display_name[48];	/* "IMG_20260927_GPS.jpg" */
	char		mime_type[32];		/* "image/jpeg", "audio/mpeg", ... */
	int		media_type;
	unsigned long	size;
	unsigned long	date_modified;
	uid_t		owner_uid;
	char		owner_pkg[24];
	int		width;
	int		height;
	int		duration_ms;
	char		artist[32];
	char		title[32];
	char		latitude[20];
	char		longitude[20];
};

static struct mp_volume vols[MAX_VOLUMES];
static struct mp_open_fh open_fhs[MAX_OPEN_FH];
static struct media_item media_db[MAX_MEDIA_ITEMS];
static int next_media_id = 1;

static unsigned long stat_fuse_ops = 0;
static unsigned long stat_scoped_denials = 0;
static unsigned long stat_exif_redactions = 0;
static unsigned long stat_binder_queries = 0;
static unsigned long stat_esp_root_queries = 0;
static unsigned long stat_esp_doc_queries = 0;
static unsigned long stat_esp_created = 0;
static unsigned long stat_esp_deleted = 0;
static unsigned long stat_esp_denials = 0;
static int h_esp = -1;

static unsigned char fuse_req_buf[20480];
static unsigned char fuse_rep_buf[20480];

static int mp_tolower(int c)
{
	return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

static void str_to_lower(char *dst, const char *src, int maxlen)
{
	int i = 0;
	while (src[i] && i < maxlen - 1) {
		dst[i] = (char)mp_tolower((unsigned char)src[i]);
		i++;
	}
	dst[i] = '\0';
}

static int str_icase_eq(const char *a, const char *b)
{
	while (*a && *b) {
		if (mp_tolower((unsigned char)*a) != mp_tolower((unsigned char)*b))
			return 0;
		a++;
		b++;
	}
	return (*a == '\0' && *b == '\0');
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

/*
 * Build a valid minimal JPEG file containing:
 *   1. SOI (FF D8)
 *   2. APP1 (FF E1) Exif segment with little-endian TIFF header ("II", 0x002A),
 *      IFD0 (ImageWidth, ImageLength, GPSInfoIFDPointer 0x8825), and a real
 *      GPS IFD (GPSLatitudeRef='N', GPSLatitude=37 deg 25' 19",
 *      GPSLongitudeRef='W', GPSLongitude=122 deg 5' 3").
 *   3. COM (FF FE) segment with human-readable EXIF metadata:
 *      "EXIF:Make=Google;Model=Pixel-SIX;GPSLatitude=37.4220N;GPSLongitude=122.0841W;"
 *   4. SOF0 (FF C0) baseline DCT header with width & height
 *   5. EOI (FF D9)
 */
static void put_le16(unsigned char *p, unsigned short v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
}

static void put_le32(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
	p[2] = (unsigned char)((v >> 16) & 0xff);
	p[3] = (unsigned char)((v >> 24) & 0xff);
}

static unsigned short get_le16(const unsigned char *p)
{
	return (unsigned short)p[0] | ((unsigned short)p[1] << 8);
}

static unsigned int get_le32(const unsigned char *p)
{
	return (unsigned int)p[0] |
	       ((unsigned int)p[1] << 8) |
	       ((unsigned int)p[2] << 16) |
	       ((unsigned int)p[3] << 24);
}

static int write_sample_geotagged_jpeg(const char *path, int width, int height,
				       const char *lat_str, const char *lon_str,
				       uid_t owner_uid)
{
	unsigned char buf[512];
	int pos = 0;
	int app1_len_pos, tiff_base, gps_ifd_off, lat_rat_off, lon_rat_off, app1_end;
	char com_str[160];
	int com_len;
	int fd;

	memset(buf, 0, sizeof(buf));

	/* SOI */
	buf[pos++] = 0xFF;
	buf[pos++] = 0xD8;

	/* APP1 marker */
	buf[pos++] = 0xFF;
	buf[pos++] = 0xE1;
	app1_len_pos = pos;
	pos += 2; /* filled in at app1_end */

	/* Exif header */
	memcpy(buf + pos, "Exif\0\0", 6);
	pos += 6;

	/* TIFF header at tiff_base */
	tiff_base = pos;
	buf[pos++] = 'I';
	buf[pos++] = 'I';
	put_le16(buf + pos, 0x002A);
	pos += 2;
	put_le32(buf + pos, 8); /* IFD0 offset = 8 */
	pos += 4;

	/* IFD0 at tiff_base + 8: 3 entries (Width, Height, GPSIFD) */
	put_le16(buf + pos, 3);
	pos += 2;

	/* Entry 0: 0x0100 ImageWidth (LONG = 4, count = 1) */
	put_le16(buf + pos, 0x0100);
	put_le16(buf + pos + 2, 4);
	put_le32(buf + pos + 4, 1);
	put_le32(buf + pos + 8, (unsigned int)width);
	pos += 12;

	/* Entry 1: 0x0101 ImageLength (LONG = 4, count = 1) */
	put_le16(buf + pos, 0x0101);
	put_le16(buf + pos + 2, 4);
	put_le32(buf + pos + 4, 1);
	put_le32(buf + pos + 8, (unsigned int)height);
	pos += 12;

	/* Entry 2: 0x8825 GPSInfoIFDPointer (LONG = 4, count = 1) */
	gps_ifd_off = (pos + 12 + 4) - tiff_base;
	put_le16(buf + pos, 0x8825);
	put_le16(buf + pos + 2, 4);
	put_le32(buf + pos + 4, 1);
	put_le32(buf + pos + 8, (unsigned int)gps_ifd_off);
	pos += 12;

	/* Next IFD offset = 0 */
	put_le32(buf + pos, 0);
	pos += 4;

	/* GPS IFD at tiff_base + gps_ifd_off: 4 entries */
	put_le16(buf + pos, 4);
	pos += 2;

	lat_rat_off = (pos + 4 * 12 + 4) - tiff_base;
	lon_rat_off = lat_rat_off + 24;

	/* GPS Tag 0x0001: GPSLatitudeRef (ASCII, count=2, inline "N\0") */
	put_le16(buf + pos, 0x0001);
	put_le16(buf + pos + 2, 2);
	put_le32(buf + pos + 4, 2);
	buf[pos + 8] = 'N';
	buf[pos + 9] = '\0';
	pos += 12;

	/* GPS Tag 0x0002: GPSLatitude (RATIONAL=5, count=3, offset=lat_rat_off) */
	put_le16(buf + pos, 0x0002);
	put_le16(buf + pos + 2, 5);
	put_le32(buf + pos + 4, 3);
	put_le32(buf + pos + 8, (unsigned int)lat_rat_off);
	pos += 12;

	/* GPS Tag 0x0003: GPSLongitudeRef (ASCII, count=2, inline "W\0") */
	put_le16(buf + pos, 0x0003);
	put_le16(buf + pos + 2, 2);
	put_le32(buf + pos + 4, 2);
	buf[pos + 8] = 'W';
	buf[pos + 9] = '\0';
	pos += 12;

	/* GPS Tag 0x0004: GPSLongitude (RATIONAL=5, count=3, offset=lon_rat_off) */
	put_le16(buf + pos, 0x0004);
	put_le16(buf + pos + 2, 5);
	put_le32(buf + pos + 4, 3);
	put_le32(buf + pos + 8, (unsigned int)lon_rat_off);
	pos += 12;

	/* End of GPS IFD */
	put_le32(buf + pos, 0);
	pos += 4;

	/* Latitude rationals: 37/1 deg, 25/1 min, 19/1 sec (37.4220N) */
	put_le32(buf + pos, 37);     put_le32(buf + pos + 4, 1);
	put_le32(buf + pos + 8, 25); put_le32(buf + pos + 12, 1);
	put_le32(buf + pos + 16, 19); put_le32(buf + pos + 20, 1);
	pos += 24;

	/* Longitude rationals: 122/1 deg, 5/1 min, 3/1 sec (122.0841W) */
	put_le32(buf + pos, 122);    put_le32(buf + pos + 4, 1);
	put_le32(buf + pos + 8, 5);  put_le32(buf + pos + 12, 1);
	put_le32(buf + pos + 16, 3); put_le32(buf + pos + 20, 1);
	pos += 24;

	app1_end = pos;
	buf[app1_len_pos] = (unsigned char)(((app1_end - app1_len_pos) >> 8) & 0xff);
	buf[app1_len_pos + 1] = (unsigned char)((app1_end - app1_len_pos) & 0xff);

	/* COM (0xFF 0xFE) ASCII EXIF summary segment for easy shell inspection */
	snprintf(com_str, sizeof(com_str),
		 "\nEXIF:Make=Google;Model=Pixel-SIX;GPSLatitude=%s;GPSLongitude=%s;\n",
		 lat_str, lon_str);
	com_len = (int)strlen(com_str);
	buf[pos++] = 0xFF;
	buf[pos++] = 0xFE;
	buf[pos++] = (unsigned char)(((com_len + 2) >> 8) & 0xff);
	buf[pos++] = (unsigned char)((com_len + 2) & 0xff);
	memcpy(buf + pos, com_str, com_len);
	pos += com_len;

	/* SOF0 (0xFF 0xC0) baseline DCT header */
	buf[pos++] = 0xFF;
	buf[pos++] = 0xC0;
	buf[pos++] = 0x00;
	buf[pos++] = 0x0B;
	buf[pos++] = 0x08; /* 8-bit precision */
	buf[pos++] = (unsigned char)((height >> 8) & 0xff);
	buf[pos++] = (unsigned char)(height & 0xff);
	buf[pos++] = (unsigned char)((width >> 8) & 0xff);
	buf[pos++] = (unsigned char)(width & 0xff);
	buf[pos++] = 0x01;
	buf[pos++] = 0x01;
	buf[pos++] = 0x11;
	buf[pos++] = 0x00;

	/* EOI */
	buf[pos++] = 0xFF;
	buf[pos++] = 0xD9;

	fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd < 0)
		return -1;
	write(fd, buf, pos);
	close(fd);
	chown(path, owner_uid, 0);
	return 0;
}

static int write_sample_mp3(const char *path, const char *title,
			    const char *artist, const char *album, uid_t owner_uid)
{
	unsigned char buf[160];
	int fd;

	memset(buf, 0, sizeof(buf));
	/* Fake MPEG audio frame sync + header (32 bytes) */
	buf[0] = 0xFF;
	buf[1] = 0xFB;
	buf[2] = 0x90;
	buf[3] = 0x64;

	/* ID3v1 128-byte trailer at offset 32 */
	memcpy(buf + 32, "TAG", 3);
	strncpy((char *)(buf + 32 + 3), title, 30);
	strncpy((char *)(buf + 32 + 33), artist, 30);
	strncpy((char *)(buf + 32 + 63), album, 30);
	memcpy(buf + 32 + 93, "2026", 4);

	fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd < 0)
		return -1;
	write(fd, buf, sizeof(buf));
	close(fd);
	chown(path, owner_uid, 0);
	return 0;
}

/*
 * Parse EXIF & media metadata from a file on the lower filesystem, and
 * also locate binary GPS IFD byte ranges for on-the-fly FUSE_READ redaction.
 */
static void parse_jpeg_and_exif(const unsigned char *buf, int len,
				int *out_w, int *out_h,
				char *out_lat, int lat_max,
				char *out_lon, int lon_max,
				int redact_in_place, unsigned char *rw_buf)
{
	int i = 0;

	/* 1. Scan binary JPEG markers if file begins with FF D8 */
	if (len >= 4 && buf[0] == 0xFF && buf[1] == 0xD8) {
		i = 2;
		while (i + 4 <= len) {
			unsigned char marker;
			int seglen;

			if (buf[i] != 0xFF) {
				i++;
				continue;
			}
			while (i < len && buf[i] == 0xFF)
				i++;
			if (i >= len)
				break;
			marker = buf[i++];
			if (marker == 0xD9 || marker == 0xDA)
				break;
			if (i + 2 > len)
				break;
			seglen = ((int)buf[i] << 8) | (int)buf[i + 1];
			if (seglen < 2 || i + seglen > len)
				break;

			/* SOF0 (0xC0) / SOF2 (0xC2): dimensions */
			if ((marker == 0xC0 || marker == 0xC2) && seglen >= 7) {
				if (out_h && *out_h == 0)
					*out_h = ((int)buf[i + 3] << 8) | (int)buf[i + 4];
				if (out_w && *out_w == 0)
					*out_w = ((int)buf[i + 5] << 8) | (int)buf[i + 6];
			}

			/* APP1 (0xE1) Exif\0\0 segment */
			if (marker == 0xE1 && seglen >= 14 &&
			    memcmp(buf + i + 2, "Exif\0\0", 6) == 0) {
				int tiff_base = i + 8;
				int tiff_len = seglen - 8;
				if (tiff_len >= 8 && buf[tiff_base] == 'I' && buf[tiff_base + 1] == 'I') {
					unsigned int ifd0_off = get_le32(buf + tiff_base + 4);
					if (ifd0_off + 2 <= (unsigned int)tiff_len) {
						unsigned short n_entries = get_le16(buf + tiff_base + ifd0_off);
						unsigned int epos = tiff_base + ifd0_off + 2;
						unsigned int gps_off = 0;
						unsigned short e;
						for (e = 0; e < n_entries && epos + 12 <= (unsigned int)(tiff_base + tiff_len); e++, epos += 12) {
							unsigned short tag = get_le16(buf + epos);
							unsigned int val = get_le32(buf + epos + 8);
							if (tag == 0x0100 && out_w && *out_w == 0)
								*out_w = (int)val;
							else if (tag == 0x0101 && out_h && *out_h == 0)
								*out_h = (int)val;
							else if (tag == 0x8825)
								gps_off = val;
						}
						if (gps_off > 0 && gps_off + 2 <= (unsigned int)tiff_len) {
							unsigned short gps_entries = get_le16(buf + tiff_base + gps_off);
							unsigned int gpos = tiff_base + gps_off + 2;
							char lat_ref = 'N', lon_ref = 'W';
							int has_lat = 0, has_lon = 0;
							unsigned int lat_d = 0, lat_m = 0, lat_s = 0;
							unsigned int lon_d = 0, lon_m = 0, lon_s = 0;

							for (e = 0; e < gps_entries && gpos + 12 <= (unsigned int)(tiff_base + tiff_len); e++, gpos += 12) {
								unsigned short gtag = get_le16(buf + gpos);
								unsigned int goff = get_le32(buf + gpos + 8);
								if (gtag == 0x0001) {
									lat_ref = (char)buf[gpos + 8];
									if (redact_in_place && rw_buf) {
										rw_buf[gpos + 8] = 0;
										rw_buf[gpos + 9] = 0;
									}
								} else if (gtag == 0x0003) {
									lon_ref = (char)buf[gpos + 8];
									if (redact_in_place && rw_buf) {
										rw_buf[gpos + 8] = 0;
										rw_buf[gpos + 9] = 0;
									}
								} else if (gtag == 0x0002 && goff + 24 <= (unsigned int)tiff_len) {
									lat_d = get_le32(buf + tiff_base + goff);
									lat_m = get_le32(buf + tiff_base + goff + 8);
									lat_s = get_le32(buf + tiff_base + goff + 16);
									has_lat = 1;
									if (redact_in_place && rw_buf)
										memset(rw_buf + tiff_base + goff, 0, 24);
								} else if (gtag == 0x0004 && goff + 24 <= (unsigned int)tiff_len) {
									lon_d = get_le32(buf + tiff_base + goff);
									lon_m = get_le32(buf + tiff_base + goff + 8);
									lon_s = get_le32(buf + tiff_base + goff + 16);
									has_lon = 1;
									if (redact_in_place && rw_buf)
										memset(rw_buf + tiff_base + goff, 0, 24);
								}
							}
							if (has_lat && out_lat && !out_lat[0] && (lat_d || lat_m || lat_s)) {
								unsigned int frac = (lat_m * 10000) / 60 + (lat_s * 10000) / 3600;
								snprintf(out_lat, lat_max, "%u.%04u%c",
									 lat_d, frac, lat_ref ? lat_ref : 'N');
							}
							if (has_lon && out_lon && !out_lon[0] && (lon_d || lon_m || lon_s)) {
								unsigned int frac = (lon_m * 10000) / 60 + (lon_s * 10000) / 3600;
								snprintf(out_lon, lon_max, "%u.%04u%c",
									 lon_d, frac, lon_ref ? lon_ref : 'W');
							}
						}
					}
				}
			}

			i += seglen;
		}
	}

	/* 2. Also scan for ASCII EXIF GPS tags (e.g. GPSLatitude=...; or GPSLongitude=...;) */
	for (i = 0; i + 13 < len; i++) {
		if (memcmp(buf + i, "GPSLatitude=", 12) == 0) {
			int vstart = i + 12;
			int vend = vstart;
			while (vend < len && buf[vend] != ';' && buf[vend] != '\n' &&
			       buf[vend] != '\r' && buf[vend] != ' ' && buf[vend] != '\0')
				vend++;
			if (out_lat && vend > vstart) {
				int clen = vend - vstart;
				if (clen >= lat_max)
					clen = lat_max - 1;
				memcpy(out_lat, buf + vstart, clen);
				out_lat[clen] = '\0';
			}
			if (redact_in_place && rw_buf && vend > vstart) {
				const char *rep = "REDACTED";
				int k, rlen = (int)strlen(rep);
				for (k = 0; vstart + k < vend; k++)
					rw_buf[vstart + k] = (k < rlen) ? (unsigned char)rep[k] : 'X';
			}
		} else if (memcmp(buf + i, "GPSLongitude=", 13) == 0) {
			int vstart = i + 13;
			int vend = vstart;
			while (vend < len && buf[vend] != ';' && buf[vend] != '\n' &&
			       buf[vend] != '\r' && buf[vend] != ' ' && buf[vend] != '\0')
				vend++;
			if (out_lon && vend > vstart) {
				int clen = vend - vstart;
				if (clen >= lon_max)
					clen = lon_max - 1;
				memcpy(out_lon, buf + vstart, clen);
				out_lon[clen] = '\0';
			}
			if (redact_in_place && rw_buf && vend > vstart) {
				const char *rep = "REDACTED";
				int k, rlen = (int)strlen(rep);
				for (k = 0; vstart + k < vend; k++)
					rw_buf[vstart + k] = (k < rlen) ? (unsigned char)rep[k] : 'X';
			}
		}
	}
}

static int has_ext(const char *name, const char *ext)
{
	int nlen = (int)strlen(name);
	int elen = (int)strlen(ext);
	if (nlen < elen)
		return 0;
	return str_icase_eq(name + nlen - elen, ext);
}

static void classify_media_file(const char *filename, char *mime_out, int *type_out)
{
	if (has_ext(filename, ".jpg") || has_ext(filename, ".jpeg")) {
		strcpy(mime_out, "image/jpeg");
		*type_out = MEDIA_TYPE_IMAGE;
	} else if (has_ext(filename, ".png")) {
		strcpy(mime_out, "image/png");
		*type_out = MEDIA_TYPE_IMAGE;
	} else if (has_ext(filename, ".gif")) {
		strcpy(mime_out, "image/gif");
		*type_out = MEDIA_TYPE_IMAGE;
	} else if (has_ext(filename, ".bmp")) {
		strcpy(mime_out, "image/bmp");
		*type_out = MEDIA_TYPE_IMAGE;
	} else if (has_ext(filename, ".mp3")) {
		strcpy(mime_out, "audio/mpeg");
		*type_out = MEDIA_TYPE_AUDIO;
	} else if (has_ext(filename, ".wav")) {
		strcpy(mime_out, "audio/x-wav");
		*type_out = MEDIA_TYPE_AUDIO;
	} else if (has_ext(filename, ".mp4") || has_ext(filename, ".mkv")) {
		strcpy(mime_out, "video/mp4");
		*type_out = MEDIA_TYPE_VIDEO;
	} else if (has_ext(filename, ".txt") || has_ext(filename, ".md") || has_ext(filename, ".pdf")) {
		strcpy(mime_out, "text/plain");
		*type_out = MEDIA_TYPE_DOCUMENT;
	} else {
		strcpy(mime_out, "application/octet-stream");
		*type_out = MEDIA_TYPE_NONE;
	}
}

static void media_db_save(void)
{
	FILE *fp;
	int i;

	mkdir("/var", 0755);
	mkdir("/var/db", 0755);
	fp = fopen(MEDIA_DB_PATH, "w");
	if (!fp)
		return;

	fprintf(fp, "# _id|volume_name|_display_name|_data|mime_type|media_type|_size|owner_uid|owner_pkg|width|height|artist|title|latitude|longitude\n");
	for (i = 0; i < MAX_MEDIA_ITEMS; i++) {
		if (!media_db[i].in_use)
			continue;
		fprintf(fp, "%d|%s|%s|%s|%s|%d|%lu|%u|%s|%d|%d|%s|%s|%s|%s\n",
			media_db[i].id,
			media_db[i].volume_name,
			media_db[i].display_name,
			media_db[i].data_path,
			media_db[i].mime_type,
			media_db[i].media_type,
			media_db[i].size,
			(unsigned int)media_db[i].owner_uid,
			media_db[i].owner_pkg,
			media_db[i].width,
			media_db[i].height,
			media_db[i].artist[0] ? media_db[i].artist : "-",
			media_db[i].title[0] ? media_db[i].title : "-",
			media_db[i].latitude[0] ? media_db[i].latitude : "-",
			media_db[i].longitude[0] ? media_db[i].longitude : "-");
	}
	fclose(fp);
}

static struct media_item *media_db_find(const char *vol_name, const char *rel_path)
{
	int i;
	for (i = 0; i < MAX_MEDIA_ITEMS; i++) {
		if (media_db[i].in_use &&
		    str_icase_eq(media_db[i].volume_name, vol_name) &&
		    strcmp(media_db[i].rel_path, rel_path) == 0) {
			return &media_db[i];
		}
	}
	return NULL;
}

static void media_db_remove_file(const char *vol_name, const char *rel_path)
{
	struct media_item *it = media_db_find(vol_name, rel_path);
	if (it) {
		it->in_use = 0;
		media_db_save();
	}
}

static void media_db_remove_volume(const char *vol_name)
{
	int i;
	for (i = 0; i < MAX_MEDIA_ITEMS; i++) {
		if (media_db[i].in_use && str_icase_eq(media_db[i].volume_name, vol_name))
			media_db[i].in_use = 0;
	}
	media_db_save();
}

static void media_db_index_file(struct mp_volume *vol, const char *rel_path,
				int has_explicit_owner, uid_t explicit_owner)
{
	char lower_full[256];
	struct stat st;
	struct media_item *it;
	const char *base;
	int fd, n;
	unsigned char hdr[512];

	if (!rel_path || !rel_path[0])
		return;

	snprintf(lower_full, sizeof(lower_full), "%s/%s", vol->lower_path, rel_path);
	if (lstat(lower_full, &st) < 0 || !S_ISREG(st.st_mode))
		return;

	it = media_db_find(vol->vol_name, rel_path);
	if (!it) {
		int i;
		for (i = 0; i < MAX_MEDIA_ITEMS; i++) {
			if (!media_db[i].in_use) {
				it = &media_db[i];
				memset(it, 0, sizeof(*it));
				it->in_use = 1;
				it->id = next_media_id++;
				it->owner_uid = st.st_uid;
				break;
			}
		}
	}
	if (!it)
		return;

	strncpy(it->volume_name, vol->vol_name, sizeof(it->volume_name) - 1);
	strncpy(it->rel_path, rel_path, sizeof(it->rel_path) - 1);
	snprintf(it->data_path, sizeof(it->data_path), "%s/%s", vol->upper_path, rel_path);

	base = strrchr(rel_path, '/');
	base = base ? (base + 1) : rel_path;
	strncpy(it->display_name, base, sizeof(it->display_name) - 1);

	classify_media_file(base, it->mime_type, &it->media_type);
	it->size = (unsigned long)st.st_size;
	it->date_modified = (unsigned long)st.st_mtime;
	if (has_explicit_owner)
		it->owner_uid = explicit_owner;
	username_for_uid(it->owner_uid, it->owner_pkg, sizeof(it->owner_pkg));

	it->width = 0;
	it->height = 0;
	it->latitude[0] = '\0';
	it->longitude[0] = '\0';
	it->artist[0] = '\0';
	it->title[0] = '\0';

	fd = open(lower_full, O_RDONLY);
	if (fd >= 0) {
		n = read(fd, hdr, sizeof(hdr));
		if (n > 0) {
			if (it->media_type == MEDIA_TYPE_IMAGE) {
				parse_jpeg_and_exif(hdr, n, &it->width, &it->height,
						    it->latitude, sizeof(it->latitude),
						    it->longitude, sizeof(it->longitude),
						    0, NULL);
			} else if (it->media_type == MEDIA_TYPE_AUDIO && st.st_size >= 128) {
				unsigned char id3[128];
				if (lseek(fd, st.st_size - 128, SEEK_SET) >= 0 &&
				    read(fd, id3, 128) == 128 &&
				    memcmp(id3, "TAG", 3) == 0) {
					memcpy(it->title, id3 + 3, 30);
					it->title[30] = '\0';
					memcpy(it->artist, id3 + 33, 30);
					it->artist[30] = '\0';
				}
			}
		}
		close(fd);
	}
}

static void scan_dir_recursive(struct mp_volume *vol, const char *rel_dir, int depth)
{
	char lower_dir[256];
	DIR *dp;
	struct dirent *de;

	if (depth > 4)
		return;

	if (rel_dir[0])
		snprintf(lower_dir, sizeof(lower_dir), "%s/%s", vol->lower_path, rel_dir);
	else
		snprintf(lower_dir, sizeof(lower_dir), "%s", vol->lower_path);

	dp = opendir(lower_dir);
	if (!dp)
		return;

	while ((de = readdir(dp)) != NULL) {
		char child_rel[128];
		char child_lower[256];
		struct stat st;

		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0 ||
		    strcmp(de->d_name, "lost+found") == 0)
			continue;

		if (rel_dir[0])
			snprintf(child_rel, sizeof(child_rel), "%s/%s", rel_dir, de->d_name);
		else
			snprintf(child_rel, sizeof(child_rel), "%s", de->d_name);

		snprintf(child_lower, sizeof(child_lower), "%s/%s", vol->lower_path, child_rel);
		if (lstat(child_lower, &st) < 0)
			continue;

		if (S_ISDIR(st.st_mode)) {
			scan_dir_recursive(vol, child_rel, depth + 1);
		} else if (S_ISREG(st.st_mode)) {
			media_db_index_file(vol, child_rel, 0, 0);
		}
	}
	closedir(dp);
}

static void scan_volume(struct mp_volume *vol)
{
	if (!vol || !vol->active)
		return;
	scan_dir_recursive(vol, "", 0);
	media_db_save();
}

/*
 * Node table management per volume
 */
static struct mp_node *vol_get_node(struct mp_volume *vol, fuse_u64 nodeid)
{
	int i;
	for (i = 0; i < MAX_NODES_PER_VOL; i++) {
		if (vol->nodes[i].in_use && vol->nodes[i].nodeid == nodeid)
			return &vol->nodes[i];
	}
	return NULL;
}

static struct mp_node *vol_find_or_add_node(struct mp_volume *vol, const char *rel_path,
					    uid_t default_uid, gid_t default_gid)
{
	int i;
	struct media_item *mi;

	for (i = 0; i < MAX_NODES_PER_VOL; i++) {
		if (vol->nodes[i].in_use && strcmp(vol->nodes[i].rel_path, rel_path) == 0)
			return &vol->nodes[i];
	}

	mi = media_db_find(vol->vol_name, rel_path);
	if (mi)
		default_uid = mi->owner_uid;

	for (i = 0; i < MAX_NODES_PER_VOL; i++) {
		if (!vol->nodes[i].in_use) {
			vol->nodes[i].in_use = 1;
			vol->nodes[i].nodeid = (fuse_u64)(i + 1);
			strncpy(vol->nodes[i].rel_path, rel_path, sizeof(vol->nodes[i].rel_path) - 1);
			vol->nodes[i].owner_uid = default_uid;
			vol->nodes[i].owner_gid = default_gid;
			return &vol->nodes[i];
		}
	}
	return NULL;
}

/*
 * Scoped Storage Access Policy Enforcement:
 *
 * Returns 0 if allowed, -EACCES if blocked by Scoped Storage.
 */
static int check_scoped_access(struct mp_volume *vol, const char *rel_path,
			       uid_t caller_uid, int is_write,
			       int target_exists, uid_t target_owner_uid)
{
	(void)vol;

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
			if (sandbox_uid >= 0 && (int)caller_uid != sandbox_uid) {
				stat_scoped_denials++;
				return -EACCES;
			}
		}
		return 0;
	}

	/* 3. Personal document dirs (Documents/, Download/): non-root owner isolation */
	if (strncmp(rel_path, "Documents/", 10) == 0 ||
	    strncmp(rel_path, "Download/", 9) == 0) {
		if (target_exists && target_owner_uid != 0 && target_owner_uid != caller_uid) {
			stat_scoped_denials++;
			return -EACCES;
		}
	}

	/* 4. Existing file modification/deletion: only owner (or root) may modify/unlink */
	if (is_write && target_exists && rel_path[0] != '\0') {
		if (target_owner_uid != caller_uid) {
			stat_scoped_denials++;
			return -EACCES;
		}
	}

	return 0;
}

static void fill_fuse_attr(struct mp_volume *vol, struct mp_node *node,
			   const struct stat *st, struct fuse_attr *attr)
{
	memset(attr, 0, sizeof(*attr));
	attr->ino = node ? node->nodeid : FUSE_ROOT_ID;
	attr->size = (fuse_u64)st->st_size;
	attr->blocks = (fuse_u64)st->st_blocks;
	attr->atime = (fuse_u64)st->st_atime;
	attr->mtime = (fuse_u64)st->st_mtime;
	attr->ctime = (fuse_u64)st->st_ctime;
	if (S_ISDIR(st->st_mode)) {
		attr->mode = S_IFDIR | 0777;
		attr->nlink = st->st_nlink ? st->st_nlink : 2;
	} else if (S_ISLNK(st->st_mode)) {
		attr->mode = S_IFLNK | 0777;
		attr->nlink = 1;
	} else {
		attr->mode = S_IFREG | 0664;
		attr->nlink = 1;
	}
	attr->uid = node ? node->owner_uid : st->st_uid;
	attr->gid = node ? node->owner_gid : st->st_gid;
	attr->blksize = 4096;
	(void)vol;
}

static void fuse_send_reply(int fuse_fd, fuse_u64 unique, int error,
			    const void *payload, unsigned int payload_len)
{
	struct fuse_out_header *oh = (struct fuse_out_header *)fuse_rep_buf;
	unsigned int total = sizeof(struct fuse_out_header) + (error ? 0 : payload_len);

	if (total > sizeof(fuse_rep_buf))
		total = sizeof(fuse_rep_buf);

	oh->len = total;
	oh->error = error; /* negative errno or 0 */
	oh->unique = unique;
	if (!error && payload && payload_len > 0)
		memcpy(fuse_rep_buf + sizeof(struct fuse_out_header), payload, payload_len);

	write(fuse_fd, fuse_rep_buf, total);
}

static void build_lower_path(struct mp_volume *vol, const char *rel_path,
			     char *out, int out_max)
{
	if (!rel_path || !rel_path[0])
		snprintf(out, out_max, "%s", vol->lower_path);
	else
		snprintf(out, out_max, "%s/%s", vol->lower_path, rel_path);
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

static void handle_fuse_message(int vol_idx)
{
	struct mp_volume *vol = &vols[vol_idx];
	struct fuse_in_header *ih;
	unsigned char *arg_ptr;
	int n;

	n = read(vol->fuse_fd, fuse_req_buf, sizeof(fuse_req_buf));
	if (n < (int)sizeof(struct fuse_in_header)) {
		if (n < 0 && (errno == ENODEV || errno == EBADF)) {
			close(vol->fuse_fd);
			vol->fuse_fd = -1;
			vol->active = 0;
		}
		return;
	}

	ih = (struct fuse_in_header *)fuse_req_buf;
	arg_ptr = fuse_req_buf + sizeof(struct fuse_in_header);
	stat_fuse_ops++;

	switch (ih->opcode) {
	case FUSE_INIT: {
		struct fuse_init_out out;
		memset(&out, 0, sizeof(out));
		out.major = FUSE_KERNEL_VERSION;
		out.minor = FUSE_KERNEL_MINOR_VERSION;
		out.max_readahead = 16384;
		out.flags = FUSE_BIG_WRITES;
		out.max_background = 4;
		out.congestion_threshold = 3;
		out.max_write = 16384;
		out.time_gran = 1;
		fuse_send_reply(vol->fuse_fd, ih->unique, 0, &out, sizeof(out));
		break;
	}

	case FUSE_FORGET:
		/* No reply for FUSE_FORGET */
		break;

	case FUSE_STATFS: {
		struct statfs sfs;
		struct fuse_statfs_out out;
		memset(&out, 0, sizeof(out));
		if (statfs(vol->lower_path, &sfs) == 0) {
			out.st.blocks = (fuse_u64)sfs.f_blocks;
			out.st.bfree = (fuse_u64)sfs.f_bfree;
			out.st.bavail = (fuse_u64)sfs.f_bavail;
			out.st.files = (fuse_u64)sfs.f_files;
			out.st.ffree = (fuse_u64)sfs.f_ffree;
			out.st.bsize = (fuse_u32)(sfs.f_bsize ? sfs.f_bsize : 4096);
			out.st.frsize = out.st.bsize;
			out.st.namelen = 255;
		} else {
			out.st.bsize = 4096;
			out.st.frsize = 4096;
			out.st.blocks = 2048;
			out.st.bfree = 1024;
			out.st.bavail = 1024;
			out.st.namelen = 255;
		}
		fuse_send_reply(vol->fuse_fd, ih->unique, 0, &out, sizeof(out));
		break;
	}

	case FUSE_GETATTR: {
		struct mp_node *node = vol_get_node(vol, ih->nodeid);
		char lpath[256];
		struct stat st;
		struct fuse_attr_out out;
		int err;

		if (!node) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOENT, NULL, 0);
			break;
		}
		build_lower_path(vol, node->rel_path, lpath, sizeof(lpath));
		if (lstat(lpath, &st) < 0) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -errno, NULL, 0);
			break;
		}
		err = check_scoped_access(vol, node->rel_path, ih->uid, 0, 1, node->owner_uid);
		if (err) {
			fuse_send_reply(vol->fuse_fd, ih->unique, err, NULL, 0);
			break;
		}
		memset(&out, 0, sizeof(out));
		out.attr_valid = 0;
		fill_fuse_attr(vol, node, &st, &out.attr);
		fuse_send_reply(vol->fuse_fd, ih->unique, 0, &out, sizeof(out));
		break;
	}

	case FUSE_LOOKUP: {
		struct mp_node *dir_node = vol_get_node(vol, ih->nodeid);
		const char *name = (const char *)arg_ptr;
		char child_rel[128], lpath[256];
		struct stat st;
		struct mp_node *child_node;
		struct media_item *mi;
		uid_t owner_uid;
		struct fuse_entry_out out;
		int err;

		if (!dir_node) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOENT, NULL, 0);
			break;
		}
		build_child_rel(dir_node->rel_path, name, child_rel, sizeof(child_rel));
		build_lower_path(vol, child_rel, lpath, sizeof(lpath));

		if (lstat(lpath, &st) < 0) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOENT, NULL, 0);
			break;
		}

		mi = media_db_find(vol->vol_name, child_rel);
		owner_uid = mi ? mi->owner_uid : st.st_uid;
		child_node = vol_find_or_add_node(vol, child_rel, owner_uid, st.st_gid);
		if (!child_node) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOMEM, NULL, 0);
			break;
		}

		err = check_scoped_access(vol, child_rel, ih->uid, 0, 1, child_node->owner_uid);
		if (err) {
			fuse_send_reply(vol->fuse_fd, ih->unique, err, NULL, 0);
			break;
		}

		memset(&out, 0, sizeof(out));
		out.nodeid = child_node->nodeid;
		out.generation = 1;
		out.entry_valid = 0;
		out.attr_valid = 0;
		fill_fuse_attr(vol, child_node, &st, &out.attr);
		fuse_send_reply(vol->fuse_fd, ih->unique, 0, &out, sizeof(out));
		break;
	}

	case FUSE_ACCESS: {
		struct mp_node *node = vol_get_node(vol, ih->nodeid);
		struct fuse_access_in *inarg = (struct fuse_access_in *)arg_ptr;
		int is_write = (inarg->mask & 2) ? 1 : 0;
		char lpath[256];
		struct stat st;
		int is_dir = 0;
		int err;

		if (!node) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOENT, NULL, 0);
			break;
		}
		build_lower_path(vol, node->rel_path, lpath, sizeof(lpath));
		if (lstat(lpath, &st) == 0 && S_ISDIR(st.st_mode))
			is_dir = 1;
		err = check_scoped_access(vol, node->rel_path, ih->uid,
					  is_dir ? 0 : is_write,
					  is_dir ? 0 : 1, node->owner_uid);
		fuse_send_reply(vol->fuse_fd, ih->unique, err, NULL, 0);
		break;
	}

	case FUSE_OPENDIR: {
		struct mp_node *node = vol_get_node(vol, ih->nodeid);
		struct fuse_open_out out;
		int err;

		if (!node) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOENT, NULL, 0);
			break;
		}
		err = check_scoped_access(vol, node->rel_path, ih->uid, 0, 1, node->owner_uid);
		if (err) {
			fuse_send_reply(vol->fuse_fd, ih->unique, err, NULL, 0);
			break;
		}
		memset(&out, 0, sizeof(out));
		out.fh = node->nodeid;
		fuse_send_reply(vol->fuse_fd, ih->unique, 0, &out, sizeof(out));
		break;
	}

	case FUSE_READDIR: {
		struct mp_node *node = vol_get_node(vol, ih->nodeid);
		struct fuse_read_in *inarg = (struct fuse_read_in *)arg_ptr;
		char lpath[256];
		DIR *dp;
		struct dirent *de;
		unsigned int max_bytes = inarg->size;
		unsigned int out_pos = 0;
		fuse_u64 cur_idx = 0;
		unsigned char *out_data = fuse_rep_buf + sizeof(struct fuse_out_header);
		int err;

		if (!node) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOENT, NULL, 0);
			break;
		}
		err = check_scoped_access(vol, node->rel_path, ih->uid, 0, 1, node->owner_uid);
		if (err) {
			fuse_send_reply(vol->fuse_fd, ih->unique, err, NULL, 0);
			break;
		}

		if (max_bytes > sizeof(fuse_rep_buf) - sizeof(struct fuse_out_header))
			max_bytes = sizeof(fuse_rep_buf) - sizeof(struct fuse_out_header);

		build_lower_path(vol, node->rel_path, lpath, sizeof(lpath));
		dp = opendir(lpath);
		if (!dp) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -errno, NULL, 0);
			break;
		}

		while ((de = readdir(dp)) != NULL) {
			unsigned int namelen = (unsigned int)strlen(de->d_name);
			unsigned int entsize = FUSE_DIRENT_ALIGN(FUSE_NAME_OFFSET + namelen);
			struct fuse_dirent *fde;
			char child_rel[128];
			struct mp_node *cnode;

			cur_idx++;
			if (cur_idx <= inarg->offset)
				continue;

			build_child_rel(node->rel_path, de->d_name, child_rel, sizeof(child_rel));

			/* Scoped Storage: hide other users' Android/data/<user> directories */
			if (strcmp(node->rel_path, "Android/data") == 0 &&
			    strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
				if (check_scoped_access(vol, child_rel, ih->uid, 0, 1, 0) != 0)
					continue;
			}

			if (out_pos + entsize > max_bytes)
				break;

			cnode = vol_find_or_add_node(vol, child_rel, 0, 0);
			fde = (struct fuse_dirent *)(out_data + out_pos);
			memset(fde, 0, entsize);
			fde->ino = cnode ? cnode->nodeid : (de->d_ino ? (fuse_u64)de->d_ino : cur_idx);
			fde->off = cur_idx;
			fde->namelen = namelen;
			fde->type = 0;
			memcpy(fde->name, de->d_name, namelen);
			out_pos += entsize;
		}
		closedir(dp);

		{
			struct fuse_out_header *oh = (struct fuse_out_header *)fuse_rep_buf;
			oh->len = sizeof(struct fuse_out_header) + out_pos;
			oh->error = 0;
			oh->unique = ih->unique;
			write(vol->fuse_fd, fuse_rep_buf, oh->len);
		}
		break;
	}

	case FUSE_RELEASEDIR:
		fuse_send_reply(vol->fuse_fd, ih->unique, 0, NULL, 0);
		break;

	case FUSE_CREATE: {
		struct mp_node *dir_node = vol_get_node(vol, ih->nodeid);
		struct fuse_create_in *inarg = (struct fuse_create_in *)arg_ptr;
		const char *name = (const char *)(arg_ptr + sizeof(struct fuse_create_in));
		char child_rel[128], lpath[256];
		struct stat st;
		int existed = 0, err, lfd, fh_idx = -1, i;
		uid_t existing_owner = 0;
		struct mp_node *child_node;
		unsigned char outbuf[sizeof(struct fuse_entry_out) + sizeof(struct fuse_open_out)];
		struct fuse_entry_out *eout = (struct fuse_entry_out *)outbuf;
		struct fuse_open_out *oout = (struct fuse_open_out *)(outbuf + sizeof(struct fuse_entry_out));

		if (!dir_node) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOENT, NULL, 0);
			break;
		}
		build_child_rel(dir_node->rel_path, name, child_rel, sizeof(child_rel));
		build_lower_path(vol, child_rel, lpath, sizeof(lpath));

		if (lstat(lpath, &st) == 0) {
			struct media_item *mi = media_db_find(vol->vol_name, child_rel);
			existed = 1;
			existing_owner = mi ? mi->owner_uid : st.st_uid;
		}

		err = check_scoped_access(vol, child_rel, ih->uid, 1, existed, existing_owner);
		if (err) {
			fuse_send_reply(vol->fuse_fd, ih->unique, err, NULL, 0);
			break;
		}

		for (i = 0; i < MAX_OPEN_FH; i++) {
			if (!open_fhs[i].in_use) {
				fh_idx = i;
				break;
			}
		}
		if (fh_idx < 0) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -EMFILE, NULL, 0);
			break;
		}

		lfd = open(lpath, O_CREAT | O_RDWR | (inarg->flags & O_TRUNC),
			   (inarg->mode & 0777) ? (inarg->mode & 0777) : 0644);
		if (lfd < 0) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -errno, NULL, 0);
			break;
		}
		if (!existed)
			chown(lpath, ih->uid, ih->gid);

		lstat(lpath, &st);
		child_node = vol_find_or_add_node(vol, child_rel,
						  existed ? existing_owner : ih->uid,
						  ih->gid);
		if (!child_node) {
			close(lfd);
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOMEM, NULL, 0);
			break;
		}
		if (!existed) {
			child_node->owner_uid = ih->uid;
			child_node->owner_gid = ih->gid;
		}

		open_fhs[fh_idx].in_use = 1;
		open_fhs[fh_idx].lower_fd = lfd;
		open_fhs[fh_idx].vol_idx = vol_idx;
		open_fhs[fh_idx].nodeid = child_node->nodeid;
		open_fhs[fh_idx].was_written = 1;

		media_db_index_file(vol, child_rel, 1, child_node->owner_uid);
		media_db_save();

		memset(outbuf, 0, sizeof(outbuf));
		eout->nodeid = child_node->nodeid;
		eout->generation = 1;
		eout->entry_valid = 0;
		eout->attr_valid = 0;
		fill_fuse_attr(vol, child_node, &st, &eout->attr);
		oout->fh = (fuse_u64)(fh_idx + 1);
		oout->open_flags = FOPEN_DIRECT_IO;

		fuse_send_reply(vol->fuse_fd, ih->unique, 0, outbuf, sizeof(outbuf));
		break;
	}

	case FUSE_MKNOD: {
		struct mp_node *dir_node = vol_get_node(vol, ih->nodeid);
		struct fuse_mknod_in *inarg = (struct fuse_mknod_in *)arg_ptr;
		const char *name = (const char *)(arg_ptr + sizeof(struct fuse_mknod_in));
		char child_rel[128], lpath[256];
		struct stat st;
		struct mp_node *child_node;
		struct fuse_entry_out out;
		int err, lfd;

		if (!dir_node) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOENT, NULL, 0);
			break;
		}
		build_child_rel(dir_node->rel_path, name, child_rel, sizeof(child_rel));
		build_lower_path(vol, child_rel, lpath, sizeof(lpath));

		err = check_scoped_access(vol, child_rel, ih->uid, 1, 0, ih->uid);
		if (err) {
			fuse_send_reply(vol->fuse_fd, ih->unique, err, NULL, 0);
			break;
		}

		lfd = open(lpath, O_CREAT | O_WRONLY | O_TRUNC,
			   (inarg->mode & 0777) ? (inarg->mode & 0777) : 0644);
		if (lfd < 0) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -errno, NULL, 0);
			break;
		}
		close(lfd);
		chown(lpath, ih->uid, ih->gid);
		lstat(lpath, &st);

		child_node = vol_find_or_add_node(vol, child_rel, ih->uid, ih->gid);
		if (!child_node) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOMEM, NULL, 0);
			break;
		}
		child_node->owner_uid = ih->uid;
		child_node->owner_gid = ih->gid;

		media_db_index_file(vol, child_rel, 1, ih->uid);
		media_db_save();

		memset(&out, 0, sizeof(out));
		out.nodeid = child_node->nodeid;
		out.generation = 1;
		fill_fuse_attr(vol, child_node, &st, &out.attr);
		fuse_send_reply(vol->fuse_fd, ih->unique, 0, &out, sizeof(out));
		break;
	}

	case FUSE_OPEN: {
		struct mp_node *node = vol_get_node(vol, ih->nodeid);
		struct fuse_open_in *inarg = (struct fuse_open_in *)arg_ptr;
		char lpath[256];
		int is_write = (inarg->flags & (O_WRONLY | O_RDWR | O_TRUNC)) ? 1 : 0;
		int err, lfd, fh_idx = -1, i;
		struct fuse_open_out out;

		if (!node) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOENT, NULL, 0);
			break;
		}
		err = check_scoped_access(vol, node->rel_path, ih->uid, is_write, 1, node->owner_uid);
		if (err) {
			fuse_send_reply(vol->fuse_fd, ih->unique, err, NULL, 0);
			break;
		}

		for (i = 0; i < MAX_OPEN_FH; i++) {
			if (!open_fhs[i].in_use) {
				fh_idx = i;
				break;
			}
		}
		if (fh_idx < 0) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -EMFILE, NULL, 0);
			break;
		}

		build_lower_path(vol, node->rel_path, lpath, sizeof(lpath));
		lfd = open(lpath, is_write ? O_RDWR : O_RDONLY);
		if (lfd < 0) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -errno, NULL, 0);
			break;
		}

		open_fhs[fh_idx].in_use = 1;
		open_fhs[fh_idx].lower_fd = lfd;
		open_fhs[fh_idx].vol_idx = vol_idx;
		open_fhs[fh_idx].nodeid = node->nodeid;
		open_fhs[fh_idx].was_written = is_write;

		memset(&out, 0, sizeof(out));
		out.fh = (fuse_u64)(fh_idx + 1);
		out.open_flags = FOPEN_DIRECT_IO;
		fuse_send_reply(vol->fuse_fd, ih->unique, 0, &out, sizeof(out));
		break;
	}

	case FUSE_READ: {
		struct mp_node *node = vol_get_node(vol, ih->nodeid);
		struct fuse_read_in *inarg = (struct fuse_read_in *)arg_ptr;
		int fh_idx = (int)inarg->fh - 1;
		unsigned int req_sz = inarg->size;
		unsigned char *out_data = fuse_rep_buf + sizeof(struct fuse_out_header);
		int lfd = -1, temp_fd = -1, nread, err;
		char lpath[256];

		if (!node) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOENT, NULL, 0);
			break;
		}
		err = check_scoped_access(vol, node->rel_path, ih->uid, 0, 1, node->owner_uid);
		if (err) {
			fuse_send_reply(vol->fuse_fd, ih->unique, err, NULL, 0);
			break;
		}

		if (req_sz > sizeof(fuse_rep_buf) - sizeof(struct fuse_out_header))
			req_sz = sizeof(fuse_rep_buf) - sizeof(struct fuse_out_header);

		if (fh_idx >= 0 && fh_idx < MAX_OPEN_FH && open_fhs[fh_idx].in_use) {
			lfd = open_fhs[fh_idx].lower_fd;
		} else {
			build_lower_path(vol, node->rel_path, lpath, sizeof(lpath));
			temp_fd = open(lpath, O_RDONLY);
			lfd = temp_fd;
		}
		if (lfd < 0) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -EIO, NULL, 0);
			break;
		}

		lseek(lfd, (off_t)inarg->offset, SEEK_SET);
		nread = read(lfd, out_data, req_sz);
		if (temp_fd >= 0)
			close(temp_fd);

		if (nread < 0) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -errno, NULL, 0);
			break;
		}

		/*
		 * On-the-Fly EXIF GPS Redaction:
		 * If the caller is neither root (0) nor the file's owner, and the
		 * file is a JPEG image (or contains EXIF metadata), redact GPS
		 * tags in the read buffer before returning it over /dev/fuse!
		 */
		if (nread > 0 && ih->uid != 0 && ih->uid != 1023 &&
		    ih->uid != node->owner_uid && inarg->offset == 0 &&
		    (has_ext(node->rel_path, ".jpg") || has_ext(node->rel_path, ".jpeg") ||
		     (nread >= 2 && out_data[0] == 0xFF && out_data[1] == 0xD8))) {
			char dummy_lat[20] = {0}, dummy_lon[20] = {0};
			parse_jpeg_and_exif(out_data, nread, NULL, NULL,
					    dummy_lat, sizeof(dummy_lat),
					    dummy_lon, sizeof(dummy_lon),
					    1, out_data);
			if (dummy_lat[0] || dummy_lon[0])
				stat_exif_redactions++;
		}

		{
			struct fuse_out_header *oh = (struct fuse_out_header *)fuse_rep_buf;
			oh->len = sizeof(struct fuse_out_header) + (unsigned int)nread;
			oh->error = 0;
			oh->unique = ih->unique;
			write(vol->fuse_fd, fuse_rep_buf, oh->len);
		}
		break;
	}

	case FUSE_WRITE: {
		struct mp_node *node = vol_get_node(vol, ih->nodeid);
		struct fuse_write_in *inarg = (struct fuse_write_in *)arg_ptr;
		const unsigned char *data = arg_ptr + sizeof(struct fuse_write_in);
		int fh_idx = (int)inarg->fh - 1;
		int lfd = -1, nwritten, err;
		struct fuse_write_out out;

		if (!node) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOENT, NULL, 0);
			break;
		}
		err = check_scoped_access(vol, node->rel_path, ih->uid, 1, 1, node->owner_uid);
		if (err) {
			fuse_send_reply(vol->fuse_fd, ih->unique, err, NULL, 0);
			break;
		}
		if (fh_idx < 0 || fh_idx >= MAX_OPEN_FH || !open_fhs[fh_idx].in_use) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -EBADF, NULL, 0);
			break;
		}
		lfd = open_fhs[fh_idx].lower_fd;
		lseek(lfd, (off_t)inarg->offset, SEEK_SET);
		nwritten = write(lfd, data, inarg->size);
		if (nwritten < 0) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -errno, NULL, 0);
			break;
		}
		open_fhs[fh_idx].was_written = 1;
		memset(&out, 0, sizeof(out));
		out.size = (fuse_u32)nwritten;
		fuse_send_reply(vol->fuse_fd, ih->unique, 0, &out, sizeof(out));
		break;
	}

	case FUSE_SETATTR: {
		struct mp_node *node = vol_get_node(vol, ih->nodeid);
		struct fuse_setattr_in *inarg = (struct fuse_setattr_in *)arg_ptr;
		char lpath[256];
		struct stat st;
		struct fuse_attr_out out;
		int err;

		if (!node) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOENT, NULL, 0);
			break;
		}
		err = check_scoped_access(vol, node->rel_path, ih->uid, 1, 1, node->owner_uid);
		if (err) {
			fuse_send_reply(vol->fuse_fd, ih->unique, err, NULL, 0);
			break;
		}
		build_lower_path(vol, node->rel_path, lpath, sizeof(lpath));
		if (inarg->valid & FATTR_SIZE) {
			if (truncate(lpath, (off_t)inarg->size) < 0) {
				fuse_send_reply(vol->fuse_fd, ih->unique, -errno, NULL, 0);
				break;
			}
		}
		if (inarg->valid & FATTR_MODE)
			chmod(lpath, inarg->mode & 0777);
		if ((inarg->valid & FATTR_UID) && ih->uid == 0) {
			node->owner_uid = inarg->uid;
			chown(lpath, inarg->uid, node->owner_gid);
		}
		if (lstat(lpath, &st) < 0) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -errno, NULL, 0);
			break;
		}
		media_db_index_file(vol, node->rel_path, 1, node->owner_uid);
		media_db_save();

		memset(&out, 0, sizeof(out));
		fill_fuse_attr(vol, node, &st, &out.attr);
		fuse_send_reply(vol->fuse_fd, ih->unique, 0, &out, sizeof(out));
		break;
	}

	case FUSE_FLUSH:
	case FUSE_FSYNC:
	case FUSE_FSYNCDIR:
		fuse_send_reply(vol->fuse_fd, ih->unique, 0, NULL, 0);
		break;

	case FUSE_RELEASE: {
		struct mp_node *node = vol_get_node(vol, ih->nodeid);
		struct fuse_release_in *inarg = (struct fuse_release_in *)arg_ptr;
		int fh_idx = (int)inarg->fh - 1;

		if (fh_idx >= 0 && fh_idx < MAX_OPEN_FH && open_fhs[fh_idx].in_use) {
			int written = open_fhs[fh_idx].was_written;
			close(open_fhs[fh_idx].lower_fd);
			open_fhs[fh_idx].in_use = 0;
			if (written && node) {
				media_db_index_file(vol, node->rel_path, 1, node->owner_uid);
				media_db_save();
			}
		}
		fuse_send_reply(vol->fuse_fd, ih->unique, 0, NULL, 0);
		break;
	}

	case FUSE_MKDIR: {
		struct mp_node *dir_node = vol_get_node(vol, ih->nodeid);
		struct fuse_mkdir_in *inarg = (struct fuse_mkdir_in *)arg_ptr;
		const char *name = (const char *)(arg_ptr + sizeof(struct fuse_mkdir_in));
		char child_rel[128], lpath[256];
		struct stat st;
		struct mp_node *child_node;
		struct fuse_entry_out out;
		int err;

		if (!dir_node) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOENT, NULL, 0);
			break;
		}
		build_child_rel(dir_node->rel_path, name, child_rel, sizeof(child_rel));
		err = check_scoped_access(vol, child_rel, ih->uid, 1, 0, ih->uid);
		if (err) {
			fuse_send_reply(vol->fuse_fd, ih->unique, err, NULL, 0);
			break;
		}
		build_lower_path(vol, child_rel, lpath, sizeof(lpath));
		if (mkdir(lpath, (inarg->mode & 0777) ? (inarg->mode & 0777) : 0755) < 0 &&
		    errno != EEXIST) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -errno, NULL, 0);
			break;
		}
		chown(lpath, ih->uid, ih->gid);
		lstat(lpath, &st);
		child_node = vol_find_or_add_node(vol, child_rel, ih->uid, ih->gid);
		if (!child_node) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOMEM, NULL, 0);
			break;
		}
		memset(&out, 0, sizeof(out));
		out.nodeid = child_node->nodeid;
		out.generation = 1;
		fill_fuse_attr(vol, child_node, &st, &out.attr);
		fuse_send_reply(vol->fuse_fd, ih->unique, 0, &out, sizeof(out));
		break;
	}

	case FUSE_UNLINK: {
		struct mp_node *dir_node = vol_get_node(vol, ih->nodeid);
		const char *name = (const char *)arg_ptr;
		char child_rel[128], lpath[256];
		struct stat st;
		struct media_item *mi;
		struct mp_node *cnode;
		uid_t owner_uid;
		int err;

		if (!dir_node) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOENT, NULL, 0);
			break;
		}
		build_child_rel(dir_node->rel_path, name, child_rel, sizeof(child_rel));
		build_lower_path(vol, child_rel, lpath, sizeof(lpath));
		if (lstat(lpath, &st) < 0) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -errno, NULL, 0);
			break;
		}
		mi = media_db_find(vol->vol_name, child_rel);
		cnode = vol_find_or_add_node(vol, child_rel, st.st_uid, st.st_gid);
		owner_uid = mi ? mi->owner_uid : (cnode ? cnode->owner_uid : st.st_uid);

		err = check_scoped_access(vol, child_rel, ih->uid, 1, 1, owner_uid);
		if (err) {
			fuse_send_reply(vol->fuse_fd, ih->unique, err, NULL, 0);
			break;
		}
		if (unlink(lpath) < 0) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -errno, NULL, 0);
			break;
		}
		if (cnode)
			cnode->in_use = 0;
		media_db_remove_file(vol->vol_name, child_rel);
		fuse_send_reply(vol->fuse_fd, ih->unique, 0, NULL, 0);
		break;
	}

	case FUSE_RMDIR: {
		struct mp_node *dir_node = vol_get_node(vol, ih->nodeid);
		const char *name = (const char *)arg_ptr;
		char child_rel[128], lpath[256];
		struct stat st;
		int err;

		if (!dir_node) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOENT, NULL, 0);
			break;
		}
		build_child_rel(dir_node->rel_path, name, child_rel, sizeof(child_rel));
		build_lower_path(vol, child_rel, lpath, sizeof(lpath));
		if (lstat(lpath, &st) < 0) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -errno, NULL, 0);
			break;
		}
		err = check_scoped_access(vol, child_rel, ih->uid, 1, 1, st.st_uid);
		if (err) {
			fuse_send_reply(vol->fuse_fd, ih->unique, err, NULL, 0);
			break;
		}
		if (rmdir(lpath) < 0) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -errno, NULL, 0);
			break;
		}
		fuse_send_reply(vol->fuse_fd, ih->unique, 0, NULL, 0);
		break;
	}

	case FUSE_RENAME: {
		struct mp_node *old_dir = vol_get_node(vol, ih->nodeid);
		struct fuse_rename_in *inarg = (struct fuse_rename_in *)arg_ptr;
		struct mp_node *new_dir = vol_get_node(vol, inarg->newdir);
		const char *old_name = (const char *)(arg_ptr + sizeof(struct fuse_rename_in));
		const char *new_name = old_name + strlen(old_name) + 1;
		char old_rel[128], new_rel[128], old_lpath[256], new_lpath[256];
		struct stat st;
		struct media_item *mi;
		uid_t owner_uid;
		int err;

		if (!old_dir || !new_dir) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -ENOENT, NULL, 0);
			break;
		}
		build_child_rel(old_dir->rel_path, old_name, old_rel, sizeof(old_rel));
		build_child_rel(new_dir->rel_path, new_name, new_rel, sizeof(new_rel));
		build_lower_path(vol, old_rel, old_lpath, sizeof(old_lpath));
		build_lower_path(vol, new_rel, new_lpath, sizeof(new_lpath));

		if (lstat(old_lpath, &st) < 0) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -errno, NULL, 0);
			break;
		}
		mi = media_db_find(vol->vol_name, old_rel);
		owner_uid = mi ? mi->owner_uid : st.st_uid;

		err = check_scoped_access(vol, old_rel, ih->uid, 1, 1, owner_uid);
		if (!err)
			err = check_scoped_access(vol, new_rel, ih->uid, 1, 0, ih->uid);
		if (err) {
			fuse_send_reply(vol->fuse_fd, ih->unique, err, NULL, 0);
			break;
		}
		if (rename(old_lpath, new_lpath) < 0) {
			fuse_send_reply(vol->fuse_fd, ih->unique, -errno, NULL, 0);
			break;
		}
		media_db_remove_file(vol->vol_name, old_rel);
		media_db_index_file(vol, new_rel, 1, owner_uid);
		media_db_save();
		fuse_send_reply(vol->fuse_fd, ih->unique, 0, NULL, 0);
		break;
	}

	case FUSE_DESTROY:
		fuse_send_reply(vol->fuse_fd, ih->unique, 0, NULL, 0);
		close(vol->fuse_fd);
		vol->fuse_fd = -1;
		vol->active = 0;
		break;

	default:
		fuse_send_reply(vol->fuse_fd, ih->unique, -ENOSYS, NULL, 0);
		break;
	}
}

/*
 * Prepare standard Android Scoped Storage directory hierarchy on a lower
 * volume and seed default sample media if missing.
 */
static void prepare_lower_storage_dirs(const char *lower_path, int is_usb,
				       const char *fstype)
{
	char path[256];
	struct stat st;

	/* Lock down raw lower storage mounts so non-root users must go via FUSE */
	chmod("/data/media", 0700);
	chmod("/mnt/media_rw", 0700);
	if (fstype && strcmp(fstype, "erofs") == 0)
		return;
	chmod(lower_path, 0700);

	snprintf(path, sizeof(path), "%s/DCIM", lower_path);
	if (lstat(path, &st) < 0) {
		if (mkdir(path, 0775) < 0 && errno == EROFS)
			return;
	}
	snprintf(path, sizeof(path), "%s/DCIM/Camera", lower_path);
	if (lstat(path, &st) < 0 && mkdir(path, 0775) < 0 && errno == EROFS)
		return;
	snprintf(path, sizeof(path), "%s/Pictures", lower_path);
	if (lstat(path, &st) < 0)
		mkdir(path, 0775);
	snprintf(path, sizeof(path), "%s/Music", lower_path);
	if (lstat(path, &st) < 0)
		mkdir(path, 0775);
	snprintf(path, sizeof(path), "%s/Movies", lower_path);
	if (lstat(path, &st) < 0)
		mkdir(path, 0775);
	snprintf(path, sizeof(path), "%s/Download", lower_path);
	if (lstat(path, &st) < 0)
		mkdir(path, 0775);
	snprintf(path, sizeof(path), "%s/Documents", lower_path);
	if (lstat(path, &st) < 0)
		mkdir(path, 0775);
	snprintf(path, sizeof(path), "%s/Android", lower_path);
	if (lstat(path, &st) < 0)
		mkdir(path, 0775);
	snprintf(path, sizeof(path), "%s/Android/data", lower_path);
	if (lstat(path, &st) < 0)
		mkdir(path, 0775);
	snprintf(path, sizeof(path), "%s/Android/data/six", lower_path);
	if (lstat(path, &st) < 0) {
		int suid = uid_for_username("six");
		mkdir(path, 0775);
		if (suid > 0)
			chown(path, (uid_t)suid, 10);
	}
	snprintf(path, sizeof(path), "%s/Android/data/guest", lower_path);
	if (lstat(path, &st) < 0) {
		int guid = uid_for_username("guest");
		mkdir(path, 0775);
		if (guid > 0)
			chown(path, (uid_t)guid, 10);
	}

	if (!is_usb) {
		snprintf(path, sizeof(path), "%s/DCIM/Camera/IMG_20260927_GPS.jpg", lower_path);
		if (lstat(path, &st) < 0) {
			write_sample_geotagged_jpeg(path, 640, 480,
						    "37.4220N", "122.0841W", 0);
		}
		snprintf(path, sizeof(path), "%s/Music/synthwave.mp3", lower_path);
		if (lstat(path, &st) < 0) {
			write_sample_mp3(path, "Neon SIX", "Kernel 2.0", "Early 2000s", 0);
		}
	} else {
		snprintf(path, sizeof(path), "%s/DCIM/USB_Photo_GPS.jpg", lower_path);
		if (lstat(path, &st) < 0) {
			write_sample_geotagged_jpeg(path, 800, 600,
						    "37.4220N", "122.0841W", 0);
		}
	}
}

static int mp_mount_volume(const char *vol_id, const char *lower_path,
			   const char *upper_path, int is_usb, const char *fstype)
{
	int slot = -1, i, ffd;
	char opts[128];
	struct mp_volume *vol;

	/* Check if already mounted at this upper_path */
	for (i = 0; i < MAX_VOLUMES; i++) {
		if (vols[i].active && strcmp(vols[i].upper_path, upper_path) == 0) {
			scan_volume(&vols[i]);
			return 0;
		}
	}

	for (i = 0; i < MAX_VOLUMES; i++) {
		if (!vols[i].active) {
			slot = i;
			break;
		}
	}
	if (slot < 0)
		return -ENOMEM;

	mkdir("/storage", 0755);
	if (!is_usb)
		mkdir("/storage/emulated", 0755);
	mkdir(upper_path, 0755);

	prepare_lower_storage_dirs(lower_path, is_usb, fstype);

	ffd = open("/dev/fuse", O_RDWR);
	if (ffd < 0)
		return -errno;

	snprintf(opts, sizeof(opts),
		 "fd=%d,rootmode=40755,user_id=0,group_id=0,allow_other", ffd);

	if (mount("mediaprovider", (char *)upper_path, "fuse.mediaprovider",
		  MS_NOSUID | MS_NODEV, opts) < 0) {
		close(ffd);
		return -errno;
	}

	vol = &vols[slot];
	memset(vol, 0, sizeof(*vol));
	vol->active = 1;
	vol->fuse_fd = ffd;
	vol->is_usb = is_usb;
	strncpy(vol->vol_id, vol_id, sizeof(vol->vol_id) - 1);
	str_to_lower(vol->vol_name, vol_id, sizeof(vol->vol_name));
	strncpy(vol->lower_path, lower_path, sizeof(vol->lower_path) - 1);
	strncpy(vol->upper_path, upper_path, sizeof(vol->upper_path) - 1);

	/* Node 1 = FUSE_ROOT_ID ("") */
	vol->nodes[0].in_use = 1;
	vol->nodes[0].nodeid = FUSE_ROOT_ID;
	vol->nodes[0].rel_path[0] = '\0';
	vol->nodes[0].owner_uid = 0;
	vol->nodes[0].owner_gid = 0;

	if (!is_usb) {
		unlink("/sdcard");
		symlink(upper_path, "/sdcard");
	} else {
		unlink("/storage/usb");
		symlink(upper_path, "/storage/usb");
	}

	scan_volume(vol);
	return 0;
}

static int mp_unmount_volume(const char *vol_id_or_all)
{
	int i, j, count = 0;

	for (i = 0; i < MAX_VOLUMES; i++) {
		struct mp_volume *vol = &vols[i];
		if (!vol->active || !vol->is_usb)
			continue;
		if (strcmp(vol_id_or_all, "ALL") != 0 &&
		    !str_icase_eq(vol->vol_id, vol_id_or_all) &&
		    strcmp(vol->upper_path, vol_id_or_all) != 0)
			continue;

		for (j = 0; j < MAX_OPEN_FH; j++) {
			if (open_fhs[j].in_use && open_fhs[j].vol_idx == i) {
				if (open_fhs[j].lower_fd >= 0)
					close(open_fhs[j].lower_fd);
				open_fhs[j].in_use = 0;
			}
		}

		/* Close /dev/fuse first so fuse_put_super() does not block on FUSE_DESTROY */
		if (vol->fuse_fd >= 0) {
			close(vol->fuse_fd);
			vol->fuse_fd = -1;
		}
		umount(vol->upper_path);
		rmdir(vol->upper_path);
		unlink("/storage/usb");
		media_db_remove_volume(vol->vol_name);
		vol->active = 0;
		count++;
	}
	return count;
}

/*
 * Handle ContentProvider URI queries/inserts/deletes over /dev/binder.
 *
 * Format of IMP_QUERY msg.data:
 *   "<uri>|<projection>|<where>"
 * Examples of <uri>:
 *   content://media/external/images/media
 *   content://media/external/audio/media
 *   content://media/external/video/media
 *   content://media/external/files
 *   content://media/4a8f-9c21/images/media
 *   content://media/4a8f-9c21/files
 */
static void parse_pipe3(const char *src, char *a, int amax,
			char *b, int bmax, char *c, int cmax)
{
	const char *p1, *p2;
	a[0] = b[0] = c[0] = '\0';
	if (!src)
		return;
	p1 = strchr(src, '|');
	if (!p1) {
		strncpy(a, src, amax - 1);
		a[amax - 1] = '\0';
		return;
	}
	{
		int l1 = (int)(p1 - src);
		if (l1 >= amax) l1 = amax - 1;
		memcpy(a, src, l1);
		a[l1] = '\0';
	}
	p2 = strchr(p1 + 1, '|');
	if (!p2) {
		strncpy(b, p1 + 1, bmax - 1);
		b[bmax - 1] = '\0';
		return;
	}
	{
		int l2 = (int)(p2 - (p1 + 1));
		if (l2 >= bmax) l2 = bmax - 1;
		memcpy(b, p1 + 1, l2);
		b[l2] = '\0';
		strncpy(c, p2 + 1, cmax - 1);
		c[cmax - 1] = '\0';
	}
}

static int parse_media_uri(const char *uri, char *vol_out, int vmax,
			   int *req_media_type, int *req_id)
{
	const char *p = uri;
	const char *slash;
	int vlen;

	*req_media_type = -1; /* -1 = any */
	*req_id = -1;

	if (strncmp(p, "content://media/", 16) != 0)
		return -1;
	p += 16;
	slash = strchr(p, '/');
	if (!slash) {
		str_to_lower(vol_out, p, vmax);
		return 0;
	}
	vlen = (int)(slash - p);
	if (vlen >= vmax)
		vlen = vmax - 1;
	memcpy(vol_out, p, vlen);
	vol_out[vlen] = '\0';
	str_to_lower(vol_out, vol_out, vmax);

	p = slash + 1;
	if (strncmp(p, "images", 6) == 0)
		*req_media_type = MEDIA_TYPE_IMAGE;
	else if (strncmp(p, "audio", 5) == 0)
		*req_media_type = MEDIA_TYPE_AUDIO;
	else if (strncmp(p, "video", 5) == 0)
		*req_media_type = MEDIA_TYPE_VIDEO;
	else if (strncmp(p, "documents", 9) == 0)
		*req_media_type = MEDIA_TYPE_DOCUMENT;

	/* Check if trailing /<id> is present */
	slash = strrchr(p, '/');
	if (slash && isdigit((unsigned char)slash[1]))
		*req_id = atoi(slash + 1);
	else if (isdigit((unsigned char)p[0]))
		*req_id = atoi(p);

	return 0;
}

static int match_where_clause(const struct media_item *it, const char *where)
{
	char key[32], val[64];
	const char *eq;
	int klen, vlen;

	if (!where || !where[0])
		return 1;

	eq = strchr(where, '=');
	if (!eq)
		return 1;

	klen = (int)(eq - where);
	while (klen > 0 && (where[klen - 1] == ' ' || where[klen - 1] == '\t'))
		klen--;
	if (klen <= 0 || klen >= (int)sizeof(key))
		return 1;
	memcpy(key, where, klen);
	key[klen] = '\0';

	eq++;
	while (*eq == ' ' || *eq == '\t' || *eq == '\'' || *eq == '"')
		eq++;
	strncpy(val, eq, sizeof(val) - 1);
	val[sizeof(val) - 1] = '\0';
	vlen = (int)strlen(val);
	while (vlen > 0 && (val[vlen - 1] == '\'' || val[vlen - 1] == '"' || val[vlen - 1] == ' '))
		val[--vlen] = '\0';

	if (strcmp(key, "_id") == 0)
		return it->id == atoi(val);
	if (strcmp(key, "_display_name") == 0)
		return strcmp(it->display_name, val) == 0;
	if (strcmp(key, "mime_type") == 0)
		return strcmp(it->mime_type, val) == 0;
	if (strcmp(key, "volume_name") == 0)
		return str_icase_eq(it->volume_name, val);
	if (strcmp(key, "owner_uid") == 0)
		return (int)it->owner_uid == atoi(val);
	if (strcmp(key, "artist") == 0)
		return strstr(it->artist, val) != NULL;
	if (strcmp(key, "title") == 0)
		return strstr(it->title, val) != NULL;
	return 1;
}

static void handle_binder_request(int bfd, struct binder_ipc_msg *msg)
{
	struct binder_ipc_msg reply;

	memset(&reply, 0, sizeof(reply));
	reply.txn_id = msg->txn_id;
	reply.status = 0;

	switch (msg->code) {
	case DUMP_TRANSACTION: {
		int i, vol_cnt = 0, item_cnt = 0, pos = 0;
		for (i = 0; i < MAX_VOLUMES; i++)
			if (vols[i].active) vol_cnt++;
		for (i = 0; i < MAX_MEDIA_ITEMS; i++)
			if (media_db[i].in_use) item_cnt++;

		pos += snprintf(reply.data + pos, sizeof(reply.data) - pos,
				"MediaProvider (dumpsys media.provider)\n"
				"  PID: %d  Database: %s\n"
				"  Active FUSE Volumes: %d  Indexed Media Items: %d\n"
				"  Stats: fuse_ops=%lu binder_queries=%lu scoped_denials=%lu exif_redactions=%lu",
				getpid(), MEDIA_DB_PATH, vol_cnt, item_cnt,
				stat_fuse_ops, stat_binder_queries,
				stat_scoped_denials, stat_exif_redactions);
		for (i = 0; i < MAX_VOLUMES && pos < (int)sizeof(reply.data) - 96; i++) {
			if (!vols[i].active)
				continue;
			pos += snprintf(reply.data + pos, sizeof(reply.data) - pos,
					"\n  Volume [%d]: %s (%s -> %s, fuse.mediaprovider)",
					i, vols[i].vol_name, vols[i].lower_path,
					vols[i].upper_path);
		}
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	case PING_TRANSACTION:
	case IMP_STATUS: {
		int i, vol_cnt = 0, item_cnt = 0, pos = 0;
		for (i = 0; i < MAX_VOLUMES; i++)
			if (vols[i].active) vol_cnt++;
		for (i = 0; i < MAX_MEDIA_ITEMS; i++)
			if (media_db[i].in_use) item_cnt++;

		pos += snprintf(reply.data + pos, sizeof(reply.data) - pos,
				"MediaProvider (pid=%d, volumes=%d, indexed=%d, fuse_ops=%lu, scoped_denials=%lu, exif_redactions=%lu)",
				getpid(), vol_cnt, item_cnt,
				stat_fuse_ops, stat_scoped_denials, stat_exif_redactions);
		for (i = 0; i < MAX_VOLUMES && pos < (int)sizeof(reply.data) - 80; i++) {
			if (!vols[i].active)
				continue;
			pos += snprintf(reply.data + pos, sizeof(reply.data) - pos,
					"\n  volume %-16s : %s -> %s (fuse.mediaprovider)",
					vols[i].vol_name, vols[i].lower_path, vols[i].upper_path);
		}
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	case IMP_MOUNT_VOLUME: {
		char vid[32], lpath[80], upath[80], fstype[16];
		parse_pipe3(msg->data, vid, sizeof(vid), lpath, sizeof(lpath), fstype, sizeof(fstype));
		if (!vid[0])
			strcpy(vid, "4A8F-9C21");
		if (!lpath[0])
			snprintf(lpath, sizeof(lpath), "/mnt/media_rw/%s", vid);
		snprintf(upath, sizeof(upath), "/storage/%s", vid);

		reply.status = mp_mount_volume(vid, lpath, upath, 1, fstype);
		if (reply.status == 0) {
			snprintf(reply.data, sizeof(reply.data),
				 "Mounted FUSE volume %s (%s -> %s)", vid, lpath, upath);
		} else {
			snprintf(reply.data, sizeof(reply.data),
				 "Failed to mount FUSE volume %s (err=%d)", vid, reply.status);
		}
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	case IMP_UNMOUNT_VOLUME: {
		const char *vid = msg->data[0] ? msg->data : "ALL";
		int n = mp_unmount_volume(vid);
		snprintf(reply.data, sizeof(reply.data),
			 "Unmounted %d FUSE volume(s) for '%s'", n, vid);
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	case IMP_SCAN: {
		int i, item_cnt = 0;
		for (i = 0; i < MAX_VOLUMES; i++) {
			if (vols[i].active)
				scan_volume(&vols[i]);
		}
		for (i = 0; i < MAX_MEDIA_ITEMS; i++) {
			if (media_db[i].in_use)
				item_cnt++;
		}
		snprintf(reply.data, sizeof(reply.data),
			 "MediaScanner completed: %d media item(s) indexed in %s",
			 item_cnt, MEDIA_DB_PATH);
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	case IMP_QUERY: {
		char uri[128], proj[128], where[128], vol_filter[32];
		int req_type = -1, req_id = -1;
		int i, row_idx = 0, pos = 0;

		stat_binder_queries++;
		parse_pipe3(msg->data, uri, sizeof(uri), proj, sizeof(proj), where, sizeof(where));
		if (parse_media_uri(uri, vol_filter, sizeof(vol_filter), &req_type, &req_id) < 0) {
			reply.status = -EINVAL;
			strcpy(reply.data, "Invalid content://media URI");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		for (i = 0; i < MAX_MEDIA_ITEMS; i++) {
			struct media_item *it = &media_db[i];
			const char *lat_val, *lon_val;
			int is_owner;

			if (!it->in_use)
				continue;
			if (strcmp(vol_filter, "external") != 0 &&
			    !str_icase_eq(vol_filter, it->volume_name))
				continue;
			if (req_type >= 0 && it->media_type != req_type)
				continue;
			if (req_id >= 0 && it->id != req_id)
				continue;
			if (!match_where_clause(it, where))
				continue;

			/* Scoped Storage check: hide private Android/data/<other_user> files */
			if (check_scoped_access(NULL, it->rel_path, (uid_t)msg->sender_euid,
						0, 1, it->owner_uid) != 0)
				continue;

			is_owner = (msg->sender_euid == 0 || msg->sender_euid == 1023 ||
				    (uid_t)msg->sender_euid == it->owner_uid);
			lat_val = it->latitude[0] ? (is_owner ? it->latitude : "REDACTED") : "null";
			lon_val = it->longitude[0] ? (is_owner ? it->longitude : "REDACTED") : "null";
			if (!is_owner && (it->latitude[0] || it->longitude[0]))
				stat_exif_redactions++;

			if (pos > 0 && pos < (int)sizeof(reply.data) - 2)
				reply.data[pos++] = '\n';

			if (it->media_type == MEDIA_TYPE_IMAGE) {
				pos += snprintf(reply.data + pos, sizeof(reply.data) - pos,
						"Row: %d _id=%d, volume=%s, _display_name=%s, _data=%s, mime=%s, _size=%lu, %dx%d, owner=%s(%u), lat=%s, lon=%s",
						row_idx, it->id, it->volume_name, it->display_name,
						it->data_path, it->mime_type, it->size,
						it->width, it->height, it->owner_pkg,
						(unsigned int)it->owner_uid, lat_val, lon_val);
			} else if (it->media_type == MEDIA_TYPE_AUDIO) {
				pos += snprintf(reply.data + pos, sizeof(reply.data) - pos,
						"Row: %d _id=%d, volume=%s, _display_name=%s, _data=%s, mime=%s, _size=%lu, title=%s, artist=%s, owner=%s(%u)",
						row_idx, it->id, it->volume_name, it->display_name,
						it->data_path, it->mime_type, it->size,
						it->title[0] ? it->title : "null",
						it->artist[0] ? it->artist : "null",
						it->owner_pkg, (unsigned int)it->owner_uid);
			} else {
				pos += snprintf(reply.data + pos, sizeof(reply.data) - pos,
						"Row: %d _id=%d, volume=%s, _display_name=%s, _data=%s, mime=%s, _size=%lu, owner=%s(%u)",
						row_idx, it->id, it->volume_name, it->display_name,
						it->data_path, it->mime_type, it->size,
						it->owner_pkg, (unsigned int)it->owner_uid);
			}
			row_idx++;
			if (pos >= (int)sizeof(reply.data) - 96)
				break;
		}

		if (row_idx == 0)
			strcpy(reply.data, "No result found.");
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	case IMP_INSERT: {
		/* msg->data = "<uri>|<display_name>|<relative_path>" */
		char uri[128], dname[64], rdir[64], vol_filter[32];
		int req_type = -1, req_id = -1, i;
		struct mp_volume *target_vol = &vols[0];
		char rel_path[128], lpath[256];
		int fd;

		parse_pipe3(msg->data, uri, sizeof(uri), dname, sizeof(dname), rdir, sizeof(rdir));
		if (parse_media_uri(uri, vol_filter, sizeof(vol_filter), &req_type, &req_id) < 0 || !dname[0]) {
			reply.status = -EINVAL;
			strcpy(reply.data, "Invalid insert arguments");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		for (i = 0; i < MAX_VOLUMES; i++) {
			if (vols[i].active && str_icase_eq(vols[i].vol_name, vol_filter)) {
				target_vol = &vols[i];
				break;
			}
		}
		if (!rdir[0]) {
			if (req_type == MEDIA_TYPE_IMAGE)
				strcpy(rdir, "Pictures");
			else if (req_type == MEDIA_TYPE_AUDIO)
				strcpy(rdir, "Music");
			else
				strcpy(rdir, "Documents");
		}

		snprintf(rel_path, sizeof(rel_path), "%s/%s", rdir, dname);
		snprintf(lpath, sizeof(lpath), "%s/%s", target_vol->lower_path, rel_path);
		fd = open(lpath, O_CREAT | O_WRONLY, 0644);
		if (fd >= 0) {
			close(fd);
			chown(lpath, (uid_t)msg->sender_euid, 0);
		}
		vol_find_or_add_node(target_vol, rel_path, (uid_t)msg->sender_euid, 0);
		media_db_index_file(target_vol, rel_path, 1, (uid_t)msg->sender_euid);
		media_db_save();

		{
			struct media_item *mi = media_db_find(target_vol->vol_name, rel_path);
			snprintf(reply.data, sizeof(reply.data),
				 "content://media/%s/files/%d",
				 target_vol->vol_name, mi ? mi->id : 0);
		}
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	case IMP_DELETE: {
		char uri[128], where[128], dummy[16], vol_filter[32];
		int req_type = -1, req_id = -1, i, deleted = 0, denied = 0;

		parse_pipe3(msg->data, uri, sizeof(uri), where, sizeof(where), dummy, sizeof(dummy));
		if (parse_media_uri(uri, vol_filter, sizeof(vol_filter), &req_type, &req_id) < 0) {
			reply.status = -EINVAL;
			strcpy(reply.data, "Invalid content://media URI");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		for (i = 0; i < MAX_MEDIA_ITEMS; i++) {
			struct media_item *it = &media_db[i];
			int v;
			if (!it->in_use)
				continue;
			if (strcmp(vol_filter, "external") != 0 &&
			    !str_icase_eq(vol_filter, it->volume_name))
				continue;
			if (req_type >= 0 && it->media_type != req_type)
				continue;
			if (req_id >= 0 && it->id != req_id)
				continue;
			if (!match_where_clause(it, where))
				continue;

			if (msg->sender_euid != 0 && (uid_t)msg->sender_euid != it->owner_uid) {
				denied++;
				stat_scoped_denials++;
				continue;
			}

			for (v = 0; v < MAX_VOLUMES; v++) {
				if (vols[v].active && str_icase_eq(vols[v].vol_name, it->volume_name)) {
					char lpath[256];
					snprintf(lpath, sizeof(lpath), "%s/%s", vols[v].lower_path, it->rel_path);
					unlink(lpath);
					break;
				}
			}
			it->in_use = 0;
			deleted++;
		}
		media_db_save();
		if (deleted == 0 && denied > 0) {
			reply.status = -EACCES;
			snprintf(reply.data, sizeof(reply.data),
				 "SecurityException: uid %d cannot delete media owned by another user",
				 msg->sender_euid);
		} else {
			snprintf(reply.data, sizeof(reply.data), "Deleted %d row(s).", deleted);
		}
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	default:
		snprintf(reply.data, sizeof(reply.data),
			 "IMediaProvider[code=%u] handled by mediaproviderd", msg->code);
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	if (!(msg->flags & TF_ONE_WAY))
		ioctl(bfd, BINDER_IOC_REPLY, &reply);
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
static int parse_esp_doc_id(const char *raw_in, struct mp_volume **out_vol,
			    char *out_root_id, int root_max,
			    char *out_rel, int rel_max)
{
	char decoded[256];
	char root_part[64];
	const char *p = raw_in ? raw_in : "";
	const char *colon;
	int i = 0, dlen;

	if (out_vol)
		*out_vol = NULL;
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
		if (!vols[0].active)
			return -ENOENT;
		if (out_vol)
			*out_vol = &vols[0];
		strncpy(out_root_id, "primary", root_max - 1);
		out_root_id[root_max - 1] = '\0';
		return 0;
	}

	for (i = 0; i < MAX_VOLUMES; i++) {
		if (vols[i].active &&
		    (str_icase_eq(vols[i].vol_id, root_part) ||
		     str_icase_eq(vols[i].vol_name, root_part))) {
			if (out_vol)
				*out_vol = &vols[i];
			strncpy(out_root_id, vols[i].vol_id, root_max - 1);
			out_root_id[root_max - 1] = '\0';
			return 0;
		}
	}

	return -ENOENT;
}

static int compute_lower_used_pct(const char *path)
{
	struct statfs sfs;
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
 * Handle Storage Access Framework (SAF) ExternalStorageProvider RPCs on
 * service "externalstorage" [android.content.IDocumentsProvider], authority
 * "com.android.externalstorage.documents".
 */
static void handle_esp_binder_request(int bfd, struct binder_ipc_msg *msg)
{
	struct binder_ipc_msg reply;

	memset(&reply, 0, sizeof(reply));
	reply.txn_id = msg->txn_id;
	reply.status = 0;

	switch (msg->code) {
	case DUMP_TRANSACTION: {
		int i, root_cnt = 0, pos = 0;
		for (i = 0; i < MAX_VOLUMES; i++)
			if (vols[i].active) root_cnt++;

		pos += snprintf(reply.data + pos, sizeof(reply.data) - pos,
				"ExternalStorageProvider (dumpsys externalstorage)\n"
				"  Authority: com.android.externalstorage.documents (pid=%d)\n"
				"  Active SAF Roots: %d\n"
				"  Stats: root_queries=%lu doc_queries=%lu created=%lu deleted=%lu saf_denials=%lu",
				getpid(), root_cnt,
				stat_esp_root_queries, stat_esp_doc_queries,
				stat_esp_created, stat_esp_deleted, stat_esp_denials);
		for (i = 0; i < MAX_VOLUMES && pos < (int)sizeof(reply.data) - 96; i++) {
			const char *rid;
			if (!vols[i].active)
				continue;
			rid = vols[i].is_usb ? vols[i].vol_id : "primary";
			pos += snprintf(reply.data + pos, sizeof(reply.data) - pos,
					"\n  Root [%d]: %s (docId=%s: mount=%s)",
					i, rid, rid, vols[i].upper_path);
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
		struct binder_uevent_msg uev;
		int i, row_idx = 0, pos = 0;

		stat_esp_root_queries++;
		memset(&uev, 0, sizeof(uev));
		ioctl(bfd, BINDER_IOC_USB_STATUS, &uev);

		for (i = 0; i < MAX_VOLUMES; i++) {
			struct mp_volume *v = &vols[i];
			const char *rid;
			char title[48];
			const char *fst;
			int pct;

			if (!v->active)
				continue;
			pct = compute_lower_used_pct(v->lower_path);
			if (!v->is_usb) {
				rid = "primary";
				strcpy(title, "Internal (/sdcard)");
				fst = "fuse";
			} else {
				rid = v->vol_id;
				snprintf(title, sizeof(title), "%.14s (USB)",
					 uev.label[0] ? uev.label : v->vol_id);
				fst = uev.fstype[0] ? uev.fstype : "ext4";
			}

			if (pos > 0 && pos < (int)sizeof(reply.data) - 2)
				reply.data[pos++] = '\n';
			pos += snprintf(reply.data + pos, sizeof(reply.data) - pos,
					"Row: %d root_id=%s, document_id=%s:, title=%s, mount_path=%s, fstype=%s, used_pct=%d, flags=0x17",
					row_idx++, rid, rid, title, v->upper_path, fst, pct);
		}
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	case IESP_QUERY_DOCUMENT: {
		struct mp_volume *vol = NULL;
		char root_id[32], rel_path[160], lower_full[256], upper_full[256];
		char mime[32], owner_name[24];
		const char *disp_name;
		struct stat st;
		struct mp_node *node;
		uid_t owner_uid;
		int mtype = 0, is_d;

		stat_esp_doc_queries++;
		if (parse_esp_doc_id(msg->data, &vol, root_id, sizeof(root_id),
				     rel_path, sizeof(rel_path)) < 0 || !vol) {
			reply.status = -ENOENT;
			strcpy(reply.data, "Document root not found");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		build_lower_path(vol, rel_path, lower_full, sizeof(lower_full));
		if (lstat(lower_full, &st) < 0) {
			reply.status = -ENOENT;
			strcpy(reply.data, "Document not found");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		node = vol_find_or_add_node(vol, rel_path, st.st_uid, st.st_gid);
		owner_uid = node ? node->owner_uid : st.st_uid;

		if (check_scoped_access(vol, rel_path, (uid_t)msg->sender_euid, 0, 1, owner_uid) < 0) {
			stat_esp_denials++;
			reply.status = -EACCES;
			snprintf(reply.data, sizeof(reply.data),
				 "SecurityException: Scoped Storage denied access to %s:%s",
				 root_id, rel_path);
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		is_d = S_ISDIR(st.st_mode) ? 1 : 0;
		if (is_d) {
			strcpy(mime, "vnd.android.document/directory");
		} else {
			classify_media_file(rel_path, mime, &mtype);
			if (strcmp(mime, "application/octet-stream") == 0)
				strcpy(mime, "text/plain");
		}

		if (!rel_path[0]) {
			disp_name = root_id;
			snprintf(upper_full, sizeof(upper_full), "%s", vol->upper_path);
		} else {
			const char *sl = strrchr(rel_path, '/');
			disp_name = sl ? (sl + 1) : rel_path;
			snprintf(upper_full, sizeof(upper_full), "%s/%s", vol->upper_path, rel_path);
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
		struct mp_volume *vol = NULL;
		char root_id[32], rel_path[160], lower_dir[256];
		DIR *dp;
		struct dirent *de;
		int pass, row_idx = 0, pos = 0;

		stat_esp_doc_queries++;
		if (parse_esp_doc_id(msg->data, &vol, root_id, sizeof(root_id),
				     rel_path, sizeof(rel_path)) < 0 || !vol) {
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
			    check_scoped_access(vol, rel_path, (uid_t)msg->sender_euid, 0, 1, 0) < 0) {
				stat_esp_denials++;
				if (strcmp(rel_path, "Android/data") == 0 ||
				    strcmp(rel_path, "Android/obb") == 0)
					stat_scoped_denials++;
				reply.status = -EACCES;
				snprintf(reply.data, sizeof(reply.data),
					 "SecurityException: SAF access to %s:%s is restricted by ExternalStorageProvider",
					 root_id, rel_path);
				reply.data_size = (unsigned int)strlen(reply.data) + 1;
				break;
			}
		}

		build_lower_path(vol, rel_path, lower_dir, sizeof(lower_dir));
		for (pass = 0; pass < 2; pass++) {
			dp = opendir(lower_dir);
			if (!dp)
				break;
			while ((de = readdir(dp)) != NULL) {
				char child_rel[192], child_lower[256], child_upper[256];
				char mime[32], owner_name[24];
				struct stat st;
				struct mp_node *node;
				uid_t owner_uid;
				int is_d, mtype = 0;

				if (strcmp(de->d_name, ".") == 0 ||
				    strcmp(de->d_name, "..") == 0 ||
				    strcmp(de->d_name, "lost+found") == 0)
					continue;

				build_child_rel(rel_path, de->d_name, child_rel, sizeof(child_rel));
				build_lower_path(vol, child_rel, child_lower, sizeof(child_lower));
				if (lstat(child_lower, &st) < 0)
					continue;

				is_d = S_ISDIR(st.st_mode) ? 1 : 0;
				if ((pass == 0 && !is_d) || (pass == 1 && is_d))
					continue;

				node = vol_find_or_add_node(vol, child_rel, st.st_uid, st.st_gid);
				owner_uid = node ? node->owner_uid : st.st_uid;
				username_for_uid(owner_uid, owner_name, sizeof(owner_name));

				if (is_d) {
					strcpy(mime, "vnd.android.document/directory");
				} else {
					classify_media_file(de->d_name, mime, &mtype);
					if (strcmp(mime, "application/octet-stream") == 0)
						strcpy(mime, "text/plain");
				}

				snprintf(child_upper, sizeof(child_upper), "%s/%s",
					 vol->upper_path, child_rel);

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
		char root_id[32], parent_rel[160], child_rel[192], lower_full[256];
		struct mp_volume *vol = NULL;
		struct mp_node *node;

		parse_pipe4(msg->data, parent_doc, sizeof(parent_doc),
			    mime, sizeof(mime), dname, sizeof(dname),
			    init_body, sizeof(init_body));
		if (!dname[0] ||
		    parse_esp_doc_id(parent_doc, &vol, root_id, sizeof(root_id),
				     parent_rel, sizeof(parent_rel)) < 0 || !vol) {
			reply.status = -EINVAL;
			strcpy(reply.data, "Invalid createDocument arguments");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		build_child_rel(parent_rel, dname, child_rel, sizeof(child_rel));
		if (check_scoped_access(vol, child_rel, (uid_t)msg->sender_euid, 1, 0,
					(uid_t)msg->sender_euid) < 0) {
			stat_esp_denials++;
			reply.status = -EACCES;
			snprintf(reply.data, sizeof(reply.data),
				 "SecurityException: Scoped Storage denied createDocument %s:%s",
				 root_id, child_rel);
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		build_lower_path(vol, child_rel, lower_full, sizeof(lower_full));
		if (strcmp(mime, "vnd.android.document/directory") == 0 ||
		    strcmp(mime, "inode/directory") == 0) {
			if (mkdir(lower_full, 0775) < 0) {
				reply.status = -errno;
				snprintf(reply.data, sizeof(reply.data),
					 "mkdir failed for %s:%s", root_id, child_rel);
				reply.data_size = (unsigned int)strlen(reply.data) + 1;
				break;
			}
			chown(lower_full, (uid_t)msg->sender_euid, 0);
			node = vol_find_or_add_node(vol, child_rel, (uid_t)msg->sender_euid, 0);
			if (node)
				node->owner_uid = (uid_t)msg->sender_euid;
		} else {
			int fd = open(lower_full, O_CREAT | O_WRONLY | O_TRUNC, 0644);
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
			chown(lower_full, (uid_t)msg->sender_euid, 0);
			node = vol_find_or_add_node(vol, child_rel, (uid_t)msg->sender_euid, 0);
			if (node)
				node->owner_uid = (uid_t)msg->sender_euid;
			media_db_index_file(vol, child_rel, 1, (uid_t)msg->sender_euid);
			media_db_save();
		}

		stat_esp_created++;
		snprintf(reply.data, sizeof(reply.data),
			 "document_id=%s:%s _data=%s/%s",
			 root_id, child_rel, vol->upper_path, child_rel);
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	case IESP_DELETE_DOCUMENT: {
		struct mp_volume *vol = NULL;
		char root_id[32], rel_path[160], lower_full[256], owner_name[24];
		struct stat st;
		struct mp_node *node;
		uid_t owner_uid;
		int rc;

		if (parse_esp_doc_id(msg->data, &vol, root_id, sizeof(root_id),
				     rel_path, sizeof(rel_path)) < 0 || !vol || !rel_path[0]) {
			reply.status = -EINVAL;
			strcpy(reply.data, "Invalid documentId for deleteDocument");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		build_lower_path(vol, rel_path, lower_full, sizeof(lower_full));
		if (lstat(lower_full, &st) < 0) {
			reply.status = -ENOENT;
			strcpy(reply.data, "Document does not exist");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		node = vol_find_or_add_node(vol, rel_path, st.st_uid, st.st_gid);
		owner_uid = node ? node->owner_uid : st.st_uid;
		username_for_uid(owner_uid, owner_name, sizeof(owner_name));

		if (check_scoped_access(vol, rel_path, (uid_t)msg->sender_euid, 1, 1, owner_uid) < 0) {
			stat_esp_denials++;
			reply.status = -EACCES;
			snprintf(reply.data, sizeof(reply.data),
				 "SecurityException: uid %d cannot delete %s:%s owned by %s(%u)",
				 msg->sender_euid, root_id, rel_path,
				 owner_name, (unsigned int)owner_uid);
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		if (S_ISDIR(st.st_mode)) {
			rc = rmdir(lower_full);
		} else {
			rc = unlink(lower_full);
			if (rc == 0)
				media_db_remove_file(vol->vol_name, rel_path);
		}

		if (rc < 0) {
			reply.status = -errno;
			snprintf(reply.data, sizeof(reply.data),
				 "Failed to delete %s:%s (errno=%d)", root_id, rel_path, errno);
		} else {
			if (node)
				node->in_use = 0;
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
		char old_lower[256], new_lower[256];
		struct mp_volume *vol = NULL;
		struct stat st;
		struct mp_node *node;
		uid_t owner_uid;
		char *slash;

		parse_pipe4(msg->data, doc_in, sizeof(doc_in),
			    new_name, sizeof(new_name),
			    dummy1, sizeof(dummy1), dummy2, sizeof(dummy2));
		if (!new_name[0] ||
		    parse_esp_doc_id(doc_in, &vol, root_id, sizeof(root_id),
				     rel_path, sizeof(rel_path)) < 0 || !vol || !rel_path[0]) {
			reply.status = -EINVAL;
			strcpy(reply.data, "Invalid renameDocument arguments");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		build_lower_path(vol, rel_path, old_lower, sizeof(old_lower));
		if (lstat(old_lower, &st) < 0) {
			reply.status = -ENOENT;
			strcpy(reply.data, "Document does not exist");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		node = vol_find_or_add_node(vol, rel_path, st.st_uid, st.st_gid);
		owner_uid = node ? node->owner_uid : st.st_uid;
		if (check_scoped_access(vol, rel_path, (uid_t)msg->sender_euid, 1, 1, owner_uid) < 0) {
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
		build_lower_path(vol, new_rel, new_lower, sizeof(new_lower));

		if (rename(old_lower, new_lower) < 0) {
			reply.status = -errno;
			strcpy(reply.data, "rename failed");
			reply.data_size = (unsigned int)strlen(reply.data) + 1;
			break;
		}

		if (node) {
			strncpy(node->rel_path, new_rel, sizeof(node->rel_path) - 1);
			node->rel_path[sizeof(node->rel_path) - 1] = '\0';
		}
		if (S_ISREG(st.st_mode)) {
			media_db_remove_file(vol->vol_name, rel_path);
			media_db_index_file(vol, new_rel, 1, owner_uid);
			media_db_save();
		}

		snprintf(reply.data, sizeof(reply.data),
			 "document_id=%s:%s _data=%s/%s",
			 root_id, new_rel, vol->upper_path, new_rel);
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	case IESP_IS_CHILD_DOCUMENT: {
		char p_doc[128], c_doc[128], d1[16], d2[16];
		char p_root[32], c_root[32], p_rel[160], c_rel[160];
		struct mp_volume *v1 = NULL, *v2 = NULL;
		int is_child = 0;

		parse_pipe4(msg->data, p_doc, sizeof(p_doc), c_doc, sizeof(c_doc),
			    d1, sizeof(d1), d2, sizeof(d2));
		if (parse_esp_doc_id(p_doc, &v1, p_root, sizeof(p_root), p_rel, sizeof(p_rel)) == 0 &&
		    parse_esp_doc_id(c_doc, &v2, c_root, sizeof(c_root), c_rel, sizeof(c_rel)) == 0 &&
		    v1 && v1 == v2) {
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
			 "IDocumentsProvider[code=%u] handled by ExternalStorageProvider", msg->code);
		reply.data_size = (unsigned int)strlen(reply.data) + 1;
		break;
	}

	if (!(msg->flags & TF_ONE_WAY))
		ioctl(bfd, BINDER_IOC_REPLY, &reply);
}

int main(int argc, char **argv)
{
	int bfd;
	struct binder_service_info svc, esp_svc;

	(void)argc;
	(void)argv;

	memset(vols, 0, sizeof(vols));
	memset(open_fhs, 0, sizeof(open_fhs));
	memset(media_db, 0, sizeof(media_db));

	mkdir("/data", 0755);
	mkdir("/data/media", 0770);
	mkdir("/data/media/0", 0770);
	mkdir("/storage", 0755);
	mkdir("/storage/emulated", 0755);
	mkdir("/storage/emulated/0", 0755);
	mkdir("/var", 0755);
	mkdir("/var/db", 0755);

	/* 1. Mount internal shared storage (/data/media/0 -> /storage/emulated/0) via FUSE */
	if (mp_mount_volume("external_primary", "/data/media/0", "/storage/emulated/0", 0, "ext4") < 0) {
		printf("mediaproviderd: warning: failed to mount /storage/emulated/0 via /dev/fuse\n");
	}

	/* 2. Register "media.provider" [android.content.IMediaProvider] and
	 *    "externalstorage" [android.content.IDocumentsProvider] on /dev/binder
	 */
	bfd = open("/dev/binder", O_RDWR);
	if (bfd < 0) {
		printf("mediaproviderd: cannot open /dev/binder (errno=%d)\n", errno);
		return 1;
	}

	memset(&svc, 0, sizeof(svc));
	strcpy(svc.name, "media.provider");
	strcpy(svc.descriptor, "android.content.IMediaProvider");
	if (ioctl(bfd, BINDER_IOC_REGISTER_SVC, &svc) < 0) {
		printf("mediaproviderd: failed to register media.provider service\n");
		close(bfd);
		return 1;
	}

	memset(&esp_svc, 0, sizeof(esp_svc));
	strcpy(esp_svc.name, "externalstorage");
	strcpy(esp_svc.descriptor, "android.content.IDocumentsProvider");
	if (ioctl(bfd, BINDER_IOC_REGISTER_SVC, &esp_svc) == 0)
		h_esp = esp_svc.handle;

	printf("mediaproviderd: MediaProvider + ExternalStorageProvider ready (pid=%d, handles=%d,%d)\n",
	       getpid(), svc.handle, h_esp);
	fflush(stdout);

	/* 3. Multiplex /dev/binder and all active /dev/fuse descriptors via select() */
	for (;;) {
		fd_set rfds;
		int max_fd = bfd;
		int i, rc;

		FD_ZERO(&rfds);
		FD_SET(bfd, &rfds);
		for (i = 0; i < MAX_VOLUMES; i++) {
			if (vols[i].active && vols[i].fuse_fd >= 0) {
				FD_SET(vols[i].fuse_fd, &rfds);
				if (vols[i].fuse_fd > max_fd)
					max_fd = vols[i].fuse_fd;
			}
		}

		rc = select(max_fd + 1, &rfds, NULL, NULL, NULL);
		if (rc <= 0)
			continue;

		for (i = 0; i < MAX_VOLUMES; i++) {
			if (vols[i].active && vols[i].fuse_fd >= 0 &&
			    FD_ISSET(vols[i].fuse_fd, &rfds)) {
				handle_fuse_message(i);
			}
		}

		if (FD_ISSET(bfd, &rfds)) {
			struct binder_ipc_msg msg;
			memset(&msg, 0, sizeof(msg));
			if (ioctl(bfd, BINDER_IOC_RECV, &msg) == 0) {
				if ((h_esp > 0 && msg.target_handle == h_esp) ||
				    strcmp(msg.interface_token, "android.content.IDocumentsProvider") == 0)
					handle_esp_binder_request(bfd, &msg);
				else
					handle_binder_request(bfd, &msg);
			}
		}
	}

	close(bfd);
	return 0;
}
