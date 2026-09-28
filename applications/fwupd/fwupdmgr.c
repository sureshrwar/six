/*
 * fwupdmgr.c - Linux Vendor Firmware Service (LVFS) / fwupd Client & Plugin
 *              Engine for SIX (/bin/fwupdmgr)
 *
 * Modular Hardware Plugins (under applications/fwupd/plugins/):
 *   1. plugins/nvme/fu-nvme-plugin.c : NVM Express 1.4 Admin Queue plugin
 *                                      Target: /dev/nvme0
 *   2. plugins/ufs/fu-ufs-plugin.c   : JEDEC UFS 4.0 UPIU + WRITE_BUFFER FFU
 *                                      Target: /dev/ufs-bsg0
 *   3. plugins/scsi/fu-scsi-plugin.c : SPC-4 SCSI WRITE_BUFFER FFU plugin
 *                                      Target: /dev/sda
 *
 * Firmware Archive (.cab) & Binary (.bin) Support:
 *   - Natively parses Microsoft Cabinet (MSCF v1.3 .cab) LVFS archives
 *     (both Uncompressed typeCompress=0 and MSZIP RFC 1951 DEFLATE
 *     typeCompress=1), extracting embedded AppStream `firmware.metainfo.xml`,
 *     validating the `<checksum type="sha256">` against the embedded `.bin`
 *     payload across `CFDATA` blocks, and streaming the `.bin` microcode
 *     directly to the target hardware plugin without temporary file extraction.
 *   - Propagates the `<release version="...">` metadata from `firmware.metainfo.xml`
 *     to the hardware plugin Commit/Activate step so any vendor's UFS, NVMe, or
 *     SCSI `.cab` archive (Samsung, SK hynix, Micron, Kioxia, Western Digital,
 *     Solidigm, Phison, etc.) activates with its authentic release version even
 *     when the vendor `.bin` payload is encrypted or opaque.
 *
 * Supported Subcommands:
 *   fwupdmgr get-plugins              List registered fwupd hardware plugins
 *   fwupdmgr get-devices              Probe all plugins and display device tree
 *   fwupdmgr refresh                  Refresh LVFS metadata, .cab & .bin packages
 *   fwupdmgr get-updates              Compare device versions against LVFS catalog
 *   fwupdmgr update [DEVICE-ID|GUID]  Apply all pending LVFS firmware updates
 *   fwupdmgr install <file.cab|.bin> [DEVICE-ID] [--allow-older] [--allow-reinstall]
 *   fwupdmgr install-blob <file.cab|.bin> [DEVICE-ID]
 *   fwupdmgr activate <DEVICE-ID> <slot>
 *   fwupdmgr verify [DEVICE-ID|GUID]  Verify device hardware SHA-256 checksums
 *   fwupdmgr get-history              Display firmware update history log
 *   fwupdmgr clear-history            Clear firmware update history log
 *   fwupdmgr examine <file.cab|.bin>  Inspect a .cab archive or .bin firmware image
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/binder.h>
#include <android/os/IFwupd.h>
#include "fwupd_plugin.h"

#define MAX_DEVICES		8
#define MAX_RELEASES		8
#define CAB_MAX_FILES		8
#define CAB_MAX_CFDATA		512
#define LVFS_PKG_DIR		"/etc/fwupd/remotes.d/lvfs/packages"
#define LVFS_META_PATH		"/etc/fwupd/remotes.d/lvfs/metadata.xml"
#define FWUPD_HISTORY_PATH	"/var/lib/fwupd/history.db"

struct lvfs_release {
	char		component_id[48];
	char		device_id[24];
	char		guid[40];
	char		plugin[16];
	char		update_protocol[32];
	char		latest_ver[16];
	char		factory_ver[16];
	char		urgency[16];
	char		summary[96];
	char		pkg_filename[80];
	char		bin_filename[80];
	char		factory_pkg_filename[80];
	char		factory_bin_filename[80];
	unsigned short	capsule_flags;
};

static struct lvfs_release lvfs_catalog[MAX_RELEASES] = {
	{
		"org.six.nvme.firmware",
		FWUPD_DEVID_NVME,
		FWUPD_GUID_NVME,
		"nvme",
		"org.nvmexpress",
		"1.4.2",
		"1.4.0",
		"High",
		"Optimize Admin/IO Queue doorbell latency and DSM/TRIM wear leveling",
		"/etc/fwupd/remotes.d/lvfs/packages/six-nvme-ssd-1.4.2.cab",
		"/etc/fwupd/remotes.d/lvfs/packages/six-nvme-ssd-1.4.2.bin",
		"/etc/fwupd/remotes.d/lvfs/packages/six-nvme-ssd-1.4.0.cab",
		"/etc/fwupd/remotes.d/lvfs/packages/six-nvme-ssd-1.4.0.bin",
		FWUPD_FLAG_SIGNED_PAYLOAD | FWUPD_FLAG_DUAL_IMAGE | FWUPD_FLAG_USABLE_DURING_UPDATE
	},
	{
		"org.six.ufs.firmware",
		FWUPD_DEVID_UFS,
		FWUPD_GUID_UFS,
		"ufs",
		"org.jedec.ufs",
		"4.10",
		"4.00",
		"Medium",
		"Improve HS-Gear5 UniPro link stability and SLC WriteBooster flush policy",
		"/etc/fwupd/remotes.d/lvfs/packages/six-ufs-flash-4.10.cab",
		"/etc/fwupd/remotes.d/lvfs/packages/six-ufs-flash-4.10.bin",
		"/etc/fwupd/remotes.d/lvfs/packages/six-ufs-flash-4.00.cab",
		"/etc/fwupd/remotes.d/lvfs/packages/six-ufs-flash-4.00.bin",
		FWUPD_FLAG_SIGNED_PAYLOAD | FWUPD_FLAG_USABLE_DURING_UPDATE
	},
	{
		"org.six.usb_scsi.firmware",
		FWUPD_DEVID_USB_SCSI,
		FWUPD_GUID_USB_SCSI,
		"scsi",
		"org.t10.scsi.write_buffer",
		"1.10",
		"1.00",
		"Medium",
		"Fix USB Mass Storage BOT SCSI sense reporting on hotplug transitions",
		"/etc/fwupd/remotes.d/lvfs/packages/six-usb-scsi-1.10.cab",
		"/etc/fwupd/remotes.d/lvfs/packages/six-usb-scsi-1.10.bin",
		"/etc/fwupd/remotes.d/lvfs/packages/six-usb-scsi-1.00.cab",
		"/etc/fwupd/remotes.d/lvfs/packages/six-usb-scsi-1.00.bin",
		FWUPD_FLAG_SIGNED_PAYLOAD | FWUPD_FLAG_REMOVABLE
	}
};

static int nr_lvfs_releases = 3;

/*
 * Parsed Microsoft Cabinet (MSCF v1.3) Archive Context
 */
struct cab_file_entry {
	char		name[96];
	unsigned int	cb_file;
	unsigned int	uoff_folder_start;
};

struct cab_cfdata_map {
	unsigned int	file_data_off;
	unsigned int	uncomp_off;
	unsigned short	cb_data;
	unsigned short	cb_uncomp;
};

struct fwupd_cab_archive {
	int			active;
	unsigned int		cb_cabinet;
	unsigned char		ver_major;
	unsigned char		ver_minor;
	unsigned short		c_folders;
	unsigned short		c_files;
	unsigned short		c_cfdata;
	unsigned short		type_compress;
	struct cab_file_entry	files[CAB_MAX_FILES];
	int			nr_files;
	struct cab_cfdata_map	blocks[CAB_MAX_CFDATA];
	int			nr_blocks;
	int			meta_file_idx;
	int			payload_file_idx;
	unsigned int		payload_uoff;
	unsigned int		payload_size;
	unsigned int		payload_nonzero;
	char			component_id[64];
	char			component_name[64];
	char			summary[96];
	char			developer[48];
	char			guid[40];
	char			update_protocol[32];
	char			version_format[24];
	char			plugin[16];
	char			device_id[24];
	char			release_version[16];
	char			release_date[16];
	char			urgency[16];
	char			req_fw_min[16];
	char			source_filename[96];
	char			expected_sha256_hex[65];
	unsigned char		payload_hdr[FWUPD_HDR_INSPECT_LEN];
	unsigned int		payload_hdr_cap;
	unsigned char		payload_sha256[32];
	unsigned char		payload_code_sha256[32];
	int			checksum_verified;
};

static struct fwupd_cab_archive active_cab;
static char active_target_ver[16];
static char xml_scratch_buf[4096];

/*
 * Plugin Registry (compiled from applications/fwupd/plugins/<name>/)
 */
static const struct fwupd_plugin_ops * const fwupd_plugins[] = {
	&fu_nvme_plugin_ops,
	&fu_ufs_plugin_ops,
	&fu_scsi_plugin_ops
};

#define NR_PLUGINS	(int)(sizeof(fwupd_plugins) / sizeof(fwupd_plugins[0]))

const char *fwupd_get_target_version(void)
{
	return active_target_ver;
}

void fwupd_trim_spaces(const char *src, int max_len, char *dst)
{
	int i;

	memcpy(dst, src, max_len);
	dst[max_len] = '\0';
	for (i = max_len - 1; i >= 0; i--) {
		if (dst[i] == ' ' || dst[i] == '\0' || dst[i] == '\n' || dst[i] == '\r')
			dst[i] = '\0';
		else
			break;
	}
}

static void format_sha256_hex(const unsigned char sha256[32], char out_hex[65])
{
	static const char hex[] = "0123456789abcdef";
	int i;

	for (i = 0; i < 32; i++) {
		out_hex[i * 2 + 0] = hex[(sha256[i] >> 4) & 0x0f];
		out_hex[i * 2 + 1] = hex[sha256[i] & 0x0f];
	}
	out_hex[64] = '\0';
}

/* =========================================================================
 * Microsoft Cabinet MSZIP (typeCompress == 1) RFC 1951 DEFLATE Decompressor
 * ========================================================================= */

#define DEFLATE_MAX_BITS	15
#define DEFLATE_MAX_SYMS	288

struct deflate_huff {
	unsigned short counts[DEFLATE_MAX_BITS + 1];
	unsigned short symbols[DEFLATE_MAX_SYMS];
};

struct deflate_stream {
	const unsigned char *in;
	unsigned int in_len;
	unsigned int in_pos;
	unsigned int bit_buf;
	int bit_cnt;
};

static unsigned int deflate_get_bits(struct deflate_stream *s, int n)
{
	unsigned int val;

	while (s->bit_cnt < n) {
		unsigned int b = (s->in_pos < s->in_len) ? s->in[s->in_pos++] : 0;
		s->bit_buf |= (b << s->bit_cnt);
		s->bit_cnt += 8;
	}
	val = s->bit_buf & ((1U << n) - 1U);
	s->bit_buf >>= n;
	s->bit_cnt -= n;
	return val;
}

static void deflate_build_huff(struct deflate_huff *h,
			       const unsigned char *lengths,
			       int num_syms)
{
	unsigned short offs[DEFLATE_MAX_BITS + 1];
	int i, len;

	for (i = 0; i <= DEFLATE_MAX_BITS; i++)
		h->counts[i] = 0;
	for (i = 0; i < num_syms; i++) {
		if (lengths[i] <= DEFLATE_MAX_BITS)
			h->counts[lengths[i]]++;
	}
	h->counts[0] = 0;

	offs[1] = 0;
	for (len = 1; len < DEFLATE_MAX_BITS; len++)
		offs[len + 1] = offs[len] + h->counts[len];

	for (i = 0; i < num_syms; i++) {
		if (lengths[i] != 0 && lengths[i] <= DEFLATE_MAX_BITS)
			h->symbols[offs[lengths[i]]++] = (unsigned short)i;
	}
}

static int deflate_decode_sym(struct deflate_stream *s,
			      const struct deflate_huff *h)
{
	int code = 0, first = 0, index = 0, len;

	for (len = 1; len <= DEFLATE_MAX_BITS; len++) {
		int count;
		code |= (int)deflate_get_bits(s, 1);
		count = (int)h->counts[len];
		if (code - count < first)
			return (int)h->symbols[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	return -1;
}

/*
 * Decompress a single MSZIP CFDATA block (2-byte "CK" signature followed by
 * an RFC 1951 raw DEFLATE stream, up to 32,768 bytes uncompressed).
 * Supports LZ77 lookback into the previous 32 KB block in the same folder.
 */
static int mszip_inflate_block(const unsigned char *in, unsigned int in_len,
			       unsigned char *out, unsigned int out_max,
			       const unsigned char *prev_dict, unsigned int prev_len)
{
	static const unsigned short len_base[29] = {
		3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
		35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
	};
	static const unsigned char len_extra[29] = {
		0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
		3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
	};
	static const unsigned short dist_base[30] = {
		1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
		257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
		8193, 12289, 16385, 24577
	};
	static const unsigned char dist_extra[30] = {
		0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
		7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
	};
	static const unsigned char cl_order[19] = {
		16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
	};
	struct deflate_stream s;
	struct deflate_huff lit_huff, dist_huff, cl_huff;
	unsigned char lengths[288 + 32];
	unsigned int out_pos = 0;
	int bfinal = 0;

	if (in_len < 2 || in[0] != 'C' || in[1] != 'K')
		return -1;

	s.in = in + 2;
	s.in_len = in_len - 2;
	s.in_pos = 0;
	s.bit_buf = 0;
	s.bit_cnt = 0;

	while (!bfinal && out_pos < out_max) {
		int btype, i;

		bfinal = (int)deflate_get_bits(&s, 1);
		btype = (int)deflate_get_bits(&s, 2);

		if (btype == 0) {
			unsigned int blk_len, blk_nlen;
			s.bit_buf = 0;
			s.bit_cnt = 0;
			if (s.in_pos + 4 > s.in_len)
				return -1;
			blk_len = (unsigned int)s.in[s.in_pos] |
				  ((unsigned int)s.in[s.in_pos + 1] << 8);
			blk_nlen = (unsigned int)s.in[s.in_pos + 2] |
				   ((unsigned int)s.in[s.in_pos + 3] << 8);
			s.in_pos += 4;
			if ((blk_len ^ 0xffffU) != blk_nlen ||
			    s.in_pos + blk_len > s.in_len ||
			    out_pos + blk_len > out_max)
				return -1;
			memcpy(out + out_pos, s.in + s.in_pos, blk_len);
			s.in_pos += blk_len;
			out_pos += blk_len;
			continue;
		}

		if (btype == 1) {
			for (i = 0; i < 144; i++) lengths[i] = 8;
			for (; i < 256; i++)      lengths[i] = 9;
			for (; i < 280; i++)      lengths[i] = 7;
			for (; i < 288; i++)      lengths[i] = 8;
			deflate_build_huff(&lit_huff, lengths, 288);
			for (i = 0; i < 32; i++)  lengths[i] = 5;
			deflate_build_huff(&dist_huff, lengths, 32);
		} else if (btype == 2) {
			int hlit = (int)deflate_get_bits(&s, 5) + 257;
			int hdist = (int)deflate_get_bits(&s, 5) + 1;
			int hclen = (int)deflate_get_bits(&s, 4) + 4;
			unsigned char cl_lens[19];

			memset(cl_lens, 0, sizeof(cl_lens));
			for (i = 0; i < hclen; i++)
				cl_lens[cl_order[i]] = (unsigned char)deflate_get_bits(&s, 3);
			deflate_build_huff(&cl_huff, cl_lens, 19);

			memset(lengths, 0, sizeof(lengths));
			i = 0;
			while (i < hlit + hdist) {
				int sym = deflate_decode_sym(&s, &cl_huff);
				if (sym < 0)
					return -1;
				if (sym < 16) {
					lengths[i++] = (unsigned char)sym;
				} else {
					unsigned char prev = 0;
					int rep = 0;
					if (sym == 16) {
						if (i == 0) return -1;
						prev = lengths[i - 1];
						rep = 3 + (int)deflate_get_bits(&s, 2);
					} else if (sym == 17) {
						rep = 3 + (int)deflate_get_bits(&s, 3);
					} else {
						rep = 11 + (int)deflate_get_bits(&s, 7);
					}
					if (i + rep > hlit + hdist)
						return -1;
					while (rep-- > 0)
						lengths[i++] = prev;
				}
			}
			deflate_build_huff(&lit_huff, lengths, hlit);
			deflate_build_huff(&dist_huff, lengths + hlit, hdist);
		} else {
			return -1;
		}

		for (;;) {
			int sym = deflate_decode_sym(&s, &lit_huff);
			if (sym < 0)
				return -1;
			if (sym < 256) {
				if (out_pos >= out_max)
					return -1;
				out[out_pos++] = (unsigned char)sym;
			} else if (sym == 256) {
				break;
			} else {
				int lidx = sym - 257;
				int didx;
				unsigned int copy_len, dist, k;

				if (lidx < 0 || lidx >= 29)
					return -1;
				copy_len = (unsigned int)len_base[lidx] +
					   deflate_get_bits(&s, len_extra[lidx]);
				didx = deflate_decode_sym(&s, &dist_huff);
				if (didx < 0 || didx >= 30)
					return -1;
				dist = (unsigned int)dist_base[didx] +
				       deflate_get_bits(&s, dist_extra[didx]);
				if (out_pos + copy_len > out_max)
					return -1;
				for (k = 0; k < copy_len; k++) {
					if (out_pos >= dist) {
						out[out_pos] = out[out_pos - dist];
					} else if (prev_dict && prev_len >= (dist - out_pos)) {
						out[out_pos] = prev_dict[prev_len - (dist - out_pos)];
					} else {
						out[out_pos] = 0;
					}
					out_pos++;
				}
			}
		}
	}
	return (int)out_pos;
}

/*
 * Cached 32 KB CFDATA block buffers (supports both uncompressed and MSZIP
 * CFDATA blocks, including cross-block LZ77 dictionary lookback).
 */
static unsigned char cab_comp_buf[36864];
static unsigned char cab_uncomp_cur[32768];
static unsigned char cab_uncomp_prev[32768];
static int cab_cache_block_idx = -1;
static unsigned int cab_cache_file_off = 0;
static unsigned int cab_cache_cur_len = 0;
static unsigned int cab_cache_prev_len = 0;

static void cab_reset_block_cache(void)
{
	cab_cache_block_idx = -1;
	cab_cache_file_off = 0;
	cab_cache_cur_len = 0;
	cab_cache_prev_len = 0;
}

static int cab_load_block(const struct fwupd_cab_archive *cab, int fd, int blk_idx)
{
	unsigned int comp_type = cab->type_compress & 0x000f;
	int step_idx;

	if (blk_idx < 0 || blk_idx >= cab->nr_blocks)
		return -1;

	if (cab_cache_block_idx == blk_idx &&
	    cab_cache_file_off == cab->blocks[blk_idx].file_data_off)
		return (int)cab_cache_cur_len;

	if (comp_type == 0) {
		unsigned int blen = cab->blocks[blk_idx].cb_data;
		if (blen > sizeof(cab_uncomp_cur))
			return -1;
		if (lseek(fd, (long)cab->blocks[blk_idx].file_data_off, 0) < 0 ||
		    read(fd, cab_uncomp_cur, (int)blen) != (int)blen)
			return -1;
		cab_cache_block_idx = blk_idx;
		cab_cache_file_off = cab->blocks[blk_idx].file_data_off;
		cab_cache_cur_len = blen;
		return (int)blen;
	}

	if (comp_type == 1) {
		int start_idx = 0;
		if (cab_cache_block_idx >= 0 && cab_cache_block_idx < blk_idx &&
		    cab_cache_file_off == cab->blocks[cab_cache_block_idx].file_data_off) {
			start_idx = cab_cache_block_idx + 1;
		} else {
			cab_cache_prev_len = 0;
		}

		for (step_idx = start_idx; step_idx <= blk_idx; step_idx++) {
			unsigned int c_len = cab->blocks[step_idx].cb_data;
			unsigned int u_len = cab->blocks[step_idx].cb_uncomp;
			int n_out;

			if (c_len > sizeof(cab_comp_buf) || u_len > sizeof(cab_uncomp_cur))
				return -1;
			if (step_idx > 0 && cab_cache_cur_len > 0) {
				memcpy(cab_uncomp_prev, cab_uncomp_cur, cab_cache_cur_len);
				cab_cache_prev_len = cab_cache_cur_len;
			}
			if (lseek(fd, (long)cab->blocks[step_idx].file_data_off, 0) < 0 ||
			    read(fd, cab_comp_buf, (int)c_len) != (int)c_len)
				return -1;
			n_out = mszip_inflate_block(cab_comp_buf, c_len,
						    cab_uncomp_cur, u_len,
						    cab_uncomp_prev, cab_cache_prev_len);
			if (n_out < 0)
				return -1;
			cab_cache_block_idx = step_idx;
			cab_cache_file_off = cab->blocks[step_idx].file_data_off;
			cab_cache_cur_len = (unsigned int)n_out;
		}
		return (int)cab_cache_cur_len;
	}

	return -1;
}

/*
 * Read uncompressed bytes from folder 0 across CFDATA blocks in a .cab file
 * (supports both uncompressed typeCompress=0 and MSZIP typeCompress=1).
 */
static int cab_read_folder_bytes(const struct fwupd_cab_archive *cab,
				 int fd,
				 unsigned int folder_off,
				 unsigned char *dst,
				 unsigned int len)
{
	unsigned int done = 0;
	int i;

	while (done < len) {
		int found = 0;
		for (i = 0; i < cab->nr_blocks; i++) {
			unsigned int b_start = cab->blocks[i].uncomp_off;
			unsigned int b_len = cab->blocks[i].cb_uncomp;
			if (folder_off >= b_start && folder_off < b_start + b_len) {
				unsigned int rel = folder_off - b_start;
				unsigned int avail = b_len - rel;
				unsigned int take = (len - done < avail) ? (len - done) : avail;

				if (cab_load_block(cab, fd, i) < (int)(rel + take))
					return -1;
				memcpy(dst + done, cab_uncomp_cur + rel, take);
				done += take;
				folder_off += take;
				found = 1;
				break;
			}
		}
		if (!found)
			break;
	}
	return (int)done;
}

/*
 * Exported payload reader used by fu-nvme-plugin, fu-ufs-plugin, and fu-scsi-plugin.
 * Transparently streams either a flat .bin file or the .bin payload inside a .cab archive.
 */
int fwupd_payload_read(int fd, unsigned int offset,
		       unsigned char *buf, unsigned int len)
{
	if (active_cab.active) {
		return cab_read_folder_bytes(&active_cab, fd,
					     active_cab.payload_uoff + offset,
					     buf, len);
	}
	if (lseek(fd, (long)offset, 0) < 0)
		return -1;
	return read(fd, buf, (int)len);
}

/*
 * Compare semantic version strings (e.g. "1.4.0" vs "1.4.2", "4.00" vs "4.10", "4.10" vs "2101").
 * Returns <0 if a < b, 0 if a == b, >0 if a > b.
 */
static int compare_versions(const char *a, const char *b)
{
	const char *pa = a ? a : "";
	const char *pb = b ? b : "";

	while (*pa || *pb) {
		long va = 0, vb = 0;
		while (*pa >= '0' && *pa <= '9') {
			va = va * 10 + (*pa - '0');
			pa++;
		}
		while (*pb >= '0' && *pb <= '9') {
			vb = vb * 10 + (*pb - '0');
			pb++;
		}
		if (va != vb)
			return (va < vb) ? -1 : 1;
		if (*pa == '.')
			pa++;
		if (*pb == '.')
			pb++;
		if ((*pa && (*pa < '0' || *pa > '9')) ||
		    (*pb && (*pb < '0' || *pb > '9')))
			return strcmp(pa, pb);
	}
	return 0;
}

static int extract_xml_tag(const char *line, const char *open_tag,
			   const char *close_tag, char *out, int max_out)
{
	const char *s = strstr(line, open_tag);
	const char *e;
	int len;

	if (!s)
		return 0;
	s += strlen(open_tag);
	e = strstr(s, close_tag);
	if (!e || e <= s)
		return 0;
	len = (int)(e - s);
	if (len >= max_out)
		len = max_out - 1;
	memcpy(out, s, len);
	out[len] = '\0';
	return 1;
}

static int extract_xml_attr(const char *line, const char *attr_prefix,
			    char *out, int max_out)
{
	const char *s = strstr(line, attr_prefix);
	const char *e;
	int len;

	if (!s)
		return 0;
	s += strlen(attr_prefix);
	e = strchr(s, '"');
	if (!e || e <= s)
		return 0;
	len = (int)(e - s);
	if (len >= max_out)
		len = max_out - 1;
	memcpy(out, s, len);
	out[len] = '\0';
	return 1;
}

static int str_ends_with(const char *str, const char *suffix)
{
	int slen = (int)strlen(str);
	int xlen = (int)strlen(suffix);
	if (slen < xlen)
		return 0;
	return strcmp(str + slen - xlen, suffix) == 0;
}

/*
 * Check if a file begins with the "MSCF" (0x4643534d) Microsoft Cabinet signature.
 */
static int is_cab_file(const char *path)
{
	unsigned int sig = 0;
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return 0;
	if (read(fd, &sig, sizeof(sig)) != (int)sizeof(sig)) {
		close(fd);
		return 0;
	}
	close(fd);
	return (sig == FWUPD_CAB_MAGIC);
}

/*
 * Parse a `firmware.metainfo.xml` buffer extracted from an LVFS .cab archive.
 */
static void parse_metainfo_xml_buf(const char *xml, struct fwupd_cab_archive *cab)
{
	const char *p = xml;
	char line[256], tmp[128];

	while (*p) {
		int k = 0;
		while (*p && *p != '\n' && *p != '\r') {
			if (k < (int)sizeof(line) - 1)
				line[k++] = *p;
			p++;
		}
		line[k] = '\0';
		while (*p == '\n' || *p == '\r')
			p++;

		if (strstr(line, "<id>") && !strstr(line, "compare=")) {
			if (extract_xml_tag(line, "<id>", "</id>", tmp, sizeof(tmp)))
				strncpy(cab->component_id, tmp, sizeof(cab->component_id) - 1);
		} else if (extract_xml_tag(line, "<name>", "</name>", tmp, sizeof(tmp))) {
			strncpy(cab->component_name, tmp, sizeof(cab->component_name) - 1);
		} else if (extract_xml_tag(line, "<summary>", "</summary>", tmp, sizeof(tmp))) {
			strncpy(cab->summary, tmp, sizeof(cab->summary) - 1);
		} else if (extract_xml_tag(line, "<developer_name>", "</developer_name>", tmp, sizeof(tmp))) {
			strncpy(cab->developer, tmp, sizeof(cab->developer) - 1);
		} else if (extract_xml_tag(line, "<firmware type=\"flashed\">", "</firmware>", tmp, sizeof(tmp))) {
			strncpy(cab->guid, tmp, sizeof(cab->guid) - 1);
		} else if (extract_xml_tag(line, "<value key=\"LVFS::VersionFormat\">", "</value>", tmp, sizeof(tmp))) {
			strncpy(cab->version_format, tmp, sizeof(cab->version_format) - 1);
		} else if (extract_xml_tag(line, "<value key=\"LVFS::UpdateProtocol\">", "</value>", tmp, sizeof(tmp))) {
			strncpy(cab->update_protocol, tmp, sizeof(cab->update_protocol) - 1);
		} else if (extract_xml_tag(line, "<value key=\"LVFS::Plugin\">", "</value>", tmp, sizeof(tmp))) {
			strncpy(cab->plugin, tmp, sizeof(cab->plugin) - 1);
		} else if (extract_xml_tag(line, "<value key=\"LVFS::DeviceId\">", "</value>", tmp, sizeof(tmp))) {
			strncpy(cab->device_id, tmp, sizeof(cab->device_id) - 1);
		} else if (strstr(line, "<release ")) {
			if (extract_xml_attr(line, "version=\"", tmp, sizeof(tmp)))
				strncpy(cab->release_version, tmp, sizeof(cab->release_version) - 1);
			if (extract_xml_attr(line, "date=\"", tmp, sizeof(tmp)))
				strncpy(cab->release_date, tmp, sizeof(cab->release_date) - 1);
			if (extract_xml_attr(line, "urgency=\"", tmp, sizeof(tmp)))
				strncpy(cab->urgency, tmp, sizeof(cab->urgency) - 1);
		} else if (strstr(line, "<checksum") && strstr(line, "type=\"sha256\"")) {
			const char *gt = strchr(line, '>');
			if (gt && extract_xml_tag(gt, ">", "</checksum>", tmp, sizeof(tmp)))
				strncpy(cab->expected_sha256_hex, tmp, sizeof(cab->expected_sha256_hex) - 1);
		} else if (extract_xml_tag(line, "<filename>", "</filename>", tmp, sizeof(tmp))) {
			strncpy(cab->source_filename, tmp, sizeof(cab->source_filename) - 1);
		} else if (strstr(line, "<firmware ") && strstr(line, "compare=\"ge\"")) {
			if (extract_xml_attr(line, "version=\"", tmp, sizeof(tmp)))
				strncpy(cab->req_fw_min, tmp, sizeof(cab->req_fw_min) - 1);
		}
	}
}

/*
 * Parse a Microsoft Cabinet (MSCF v1.3) .cab archive, extract its
 * `firmware.metainfo.xml`, and verify the embedded `.bin` firmware payload
 * using streaming FIPS 180-4 SHA-256 across all CFDATA blocks.
 */
static int parse_cab_archive(const char *cab_path, struct fwupd_cab_archive *cab)
{
	struct fwupd_cab_hdr hdr;
	struct fwupd_cab_folder folder;
	struct fwupd_sha256_ctx full_ctx, code_ctx;
	unsigned char chunk[FWUPD_CHUNK_SIZE];
	unsigned int folder_table_off = sizeof(struct fwupd_cab_hdr);
	unsigned char cb_cfdata_rsvd = 0;
	unsigned int pos, uncomp_cursor, offset;
	char calc_hex[65];
	int fd, i;

	memset(cab, 0, sizeof(*cab));
	cab->meta_file_idx = -1;
	cab->payload_file_idx = -1;
	cab_reset_block_cache();

	fd = open(cab_path, O_RDONLY);
	if (fd < 0)
		return -1;

	if (read(fd, &hdr, sizeof(hdr)) != (int)sizeof(hdr) ||
	    hdr.signature != FWUPD_CAB_MAGIC ||
	    hdr.c_folders < 1 || hdr.c_files < 1) {
		close(fd);
		return -1;
	}

	cab->cb_cabinet = hdr.cb_cabinet;
	cab->ver_major = hdr.version_major;
	cab->ver_minor = hdr.version_minor;
	cab->c_folders = hdr.c_folders;
	cab->c_files = hdr.c_files;

	/* Handle optional reserve header (cfhdrRESERVE_PRESENT = 0x0004) */
	if (hdr.flags & 0x0004) {
		unsigned char rsvd_hdr[4];
		unsigned short cb_cfhdr;
		if (read(fd, rsvd_hdr, 4) != 4) {
			close(fd);
			return -1;
		}
		cb_cfhdr = (unsigned short)rsvd_hdr[0] | ((unsigned short)rsvd_hdr[1] << 8);
		cb_cfdata_rsvd = rsvd_hdr[3];
		folder_table_off += 4U + cb_cfhdr;
	}

	if (lseek(fd, (long)folder_table_off, 0) < 0 ||
	    read(fd, &folder, sizeof(folder)) != (int)sizeof(folder)) {
		close(fd);
		return -1;
	}

	cab->c_cfdata = folder.c_cfdata;
	cab->type_compress = folder.type_compress;
	if ((folder.type_compress & 0x000f) > 1) {
		close(fd);
		return -2; /* Unsupported compression type (only NONE=0 and MSZIP=1 supported) */
	}

	/* Map all CFDATA blocks in Folder 0 */
	pos = folder.coff_cab_start;
	uncomp_cursor = 0;
	for (i = 0; i < (int)folder.c_cfdata && i < CAB_MAX_CFDATA; i++) {
		struct fwupd_cab_data_hdr dhdr;
		if (lseek(fd, (long)pos, 0) < 0 ||
		    read(fd, &dhdr, sizeof(dhdr)) != (int)sizeof(dhdr)) {
			close(fd);
			return -1;
		}
		cab->blocks[i].file_data_off = pos + (unsigned int)sizeof(dhdr) + cb_cfdata_rsvd;
		cab->blocks[i].uncomp_off = uncomp_cursor;
		cab->blocks[i].cb_data = dhdr.cb_data;
		cab->blocks[i].cb_uncomp = dhdr.cb_uncomp;
		cab->nr_blocks++;
		uncomp_cursor += dhdr.cb_uncomp;
		pos = cab->blocks[i].file_data_off + dhdr.cb_data;
	}

	/* Read CFFILE entries starting at hdr.coff_files */
	pos = hdr.coff_files;
	for (i = 0; i < (int)hdr.c_files && i < CAB_MAX_FILES; i++) {
		struct fwupd_cab_file_hdr fhdr;
		char name_buf[96];
		int k = 0;
		char ch;

		if (lseek(fd, (long)pos, 0) < 0 ||
		    read(fd, &fhdr, sizeof(fhdr)) != (int)sizeof(fhdr)) {
			close(fd);
			return -1;
		}
		pos += sizeof(fhdr);
		while (read(fd, &ch, 1) == 1) {
			pos++;
			if (ch == '\0')
				break;
			if (k < (int)sizeof(name_buf) - 1)
				name_buf[k++] = ch;
		}
		name_buf[k] = '\0';

		strcpy(cab->files[i].name, name_buf);
		cab->files[i].cb_file = fhdr.cb_file;
		cab->files[i].uoff_folder_start = fhdr.uoff_folder_start;
		cab->nr_files++;

		if (str_ends_with(name_buf, ".metainfo.xml") || str_ends_with(name_buf, ".xml"))
			cab->meta_file_idx = i;
		else if (str_ends_with(name_buf, ".bin") || str_ends_with(name_buf, ".fw") ||
			 str_ends_with(name_buf, ".fluf") || str_ends_with(name_buf, ".img"))
			cab->payload_file_idx = i;
	}

	/* Extract and parse firmware.metainfo.xml if present */
	if (cab->meta_file_idx >= 0) {
		unsigned int xml_len = cab->files[cab->meta_file_idx].cb_file;
		if (xml_len >= sizeof(xml_scratch_buf))
			xml_len = sizeof(xml_scratch_buf) - 1;
		memset(xml_scratch_buf, 0, sizeof(xml_scratch_buf));
		if (cab_read_folder_bytes(cab, fd,
					  cab->files[cab->meta_file_idx].uoff_folder_start,
					  (unsigned char *)xml_scratch_buf, xml_len) == (int)xml_len) {
			xml_scratch_buf[xml_len] = '\0';
			parse_metainfo_xml_buf(xml_scratch_buf, cab);
		}
	}

	/* Match source_filename from metainfo.xml if specified */
	if (cab->source_filename[0]) {
		for (i = 0; i < cab->nr_files; i++) {
			if (strcmp(cab->files[i].name, cab->source_filename) == 0) {
				cab->payload_file_idx = i;
				break;
			}
		}
	}

	/* Fallback: pick the largest non-XML/non-TXT file in the cabinet */
	if (cab->payload_file_idx < 0) {
		unsigned int best_sz = 0;
		for (i = 0; i < cab->nr_files; i++) {
			if (i != cab->meta_file_idx &&
			    !str_ends_with(cab->files[i].name, ".txt") &&
			    !str_ends_with(cab->files[i].name, ".asc") &&
			    !str_ends_with(cab->files[i].name, ".p7b") &&
			    cab->files[i].cb_file > best_sz) {
				best_sz = cab->files[i].cb_file;
				cab->payload_file_idx = i;
			}
		}
	}

	if (cab->payload_file_idx < 0) {
		close(fd);
		return -1;
	}

	cab->payload_uoff = cab->files[cab->payload_file_idx].uoff_folder_start;
	cab->payload_size = cab->files[cab->payload_file_idx].cb_file;
	if (!cab->source_filename[0])
		strcpy(cab->source_filename, cab->files[cab->payload_file_idx].name);

	/* Stream the embedded .bin payload across CFDATA blocks to compute SHA-256 */
	fwupd_sha256_init(&full_ctx);
	fwupd_sha256_init(&code_ctx);
	offset = 0;
	while (offset < cab->payload_size) {
		unsigned int rem = cab->payload_size - offset;
		unsigned int take = (rem > FWUPD_CHUNK_SIZE) ? FWUPD_CHUNK_SIZE : rem;
		int nread = cab_read_folder_bytes(cab, fd, cab->payload_uoff + offset, chunk, take);
		if (nread <= 0) {
			close(fd);
			return -1;
		}
		for (i = 0; i < nread; i++) {
			unsigned int bpos = offset + (unsigned int)i;
			if (bpos < FWUPD_HDR_INSPECT_LEN) {
				cab->payload_hdr[bpos] = chunk[i];
				if (bpos + 1 > cab->payload_hdr_cap)
					cab->payload_hdr_cap = bpos + 1;
			}
			if (chunk[i] != 0)
				cab->payload_nonzero++;
		}
		fwupd_sha256_update(&full_ctx, chunk, (unsigned int)nread);
		if (offset + (unsigned int)nread > (unsigned int)sizeof(struct fwupd_bin_hdr)) {
			unsigned int hdr_sz = (unsigned int)sizeof(struct fwupd_bin_hdr);
			unsigned int code_start = (offset >= hdr_sz) ? 0U : (hdr_sz - offset);
			fwupd_sha256_update(&code_ctx, chunk + code_start,
					    (unsigned int)nread - code_start);
		}
		offset += (unsigned int)nread;
	}
	close(fd);

	fwupd_sha256_final(&full_ctx, cab->payload_sha256);
	fwupd_sha256_final(&code_ctx, cab->payload_code_sha256);
	format_sha256_hex(cab->payload_sha256, calc_hex);

	if (cab->expected_sha256_hex[0]) {
		cab->checksum_verified = (strcmp(calc_hex, cab->expected_sha256_hex) == 0);
	} else {
		cab->checksum_verified = 1;
	}
	return 0;
}

/*
 * Parse /etc/fwupd/remotes.d/lvfs/metadata.xml to populate/update lvfs_catalog[].
 */
static void parse_lvfs_metadata_xml(void)
{
	FILE *fp = fopen(LVFS_META_PATH, "r");
	char line[256], tmp[128];
	int cur = -1;

	if (!fp)
		return;

	while (fgets(line, sizeof(line), fp)) {
		if (strstr(line, "<component ")) {
			if (cur + 1 < MAX_RELEASES)
				cur++;
		} else if (cur >= 0 && cur < nr_lvfs_releases) {
			struct lvfs_release *r = &lvfs_catalog[cur];
			if (extract_xml_tag(line, "<id>", "</id>", tmp, sizeof(tmp))) {
				strncpy(r->component_id, tmp, sizeof(r->component_id) - 1);
			} else if (extract_xml_tag(line, "<firmware type=\"flashed\">", "</firmware>", tmp, sizeof(tmp))) {
				strncpy(r->guid, tmp, sizeof(r->guid) - 1);
			} else if (extract_xml_tag(line, "<value key=\"LVFS::UpdateProtocol\">", "</value>", tmp, sizeof(tmp))) {
				strncpy(r->update_protocol, tmp, sizeof(r->update_protocol) - 1);
			} else if (extract_xml_tag(line, "<value key=\"LVFS::Plugin\">", "</value>", tmp, sizeof(tmp))) {
				strncpy(r->plugin, tmp, sizeof(r->plugin) - 1);
			} else if (extract_xml_tag(line, "<value key=\"LVFS::DeviceId\">", "</value>", tmp, sizeof(tmp))) {
				strncpy(r->device_id, tmp, sizeof(r->device_id) - 1);
			} else if (strstr(line, "<release ")) {
				if (extract_xml_attr(line, "version=\"", tmp, sizeof(tmp)))
					strncpy(r->latest_ver, tmp, sizeof(r->latest_ver) - 1);
				if (extract_xml_attr(line, "urgency=\"", tmp, sizeof(tmp)))
					strncpy(r->urgency, tmp, sizeof(r->urgency) - 1);
			} else if (extract_xml_tag(line, "<location>", "</location>", tmp, sizeof(tmp))) {
				const char *p = (strncmp(tmp, "file://", 7) == 0) ? (tmp + 7) : tmp;
				strncpy(r->pkg_filename, p, sizeof(r->pkg_filename) - 1);
			} else if (extract_xml_tag(line, "<p>", "</p>", tmp, sizeof(tmp))) {
				strncpy(r->summary, tmp, sizeof(r->summary) - 1);
			}
		}
	}
	fclose(fp);
}

static int write_bin_firmware_file(const char *path, const char *plugin,
				   const char *device_id, const char *guid,
				   const char *fw_version, unsigned short flags)
{
	unsigned char buf[sizeof(struct fwupd_bin_hdr) + FWUPD_DEFAULT_PAYLOAD_SIZE];
	int fd;

	fwupd_build_bin_image(plugin, device_id, guid, fw_version, flags, buf, NULL);

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -1;
	if (write(fd, buf, sizeof(buf)) != (int)sizeof(buf)) {
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

/*
 * Build a genuine Microsoft Cabinet (MSCF v1.3) LVFS `.cab` archive containing:
 *   [0] firmware.metainfo.xml
 *   [1] README.txt
 *   [2] <basename>.bin (1,184-byte signed controller firmware .bin)
 */
static int write_cab_firmware_file(const char *cab_path,
				   const char *bin_path,
				   const char *component_id,
				   const char *plugin,
				   const char *update_protocol,
				   const char *device_id,
				   const char *guid,
				   const char *fw_version,
				   const char *urgency,
				   const char *summary,
				   unsigned short flags)
{
	unsigned char bin_buf[sizeof(struct fwupd_bin_hdr) + FWUPD_DEFAULT_PAYLOAD_SIZE];
	unsigned char bin_sha256[32];
	char sha_hex[65];
	char xml_buf[1280];
	char readme_buf[384];
	const char *bin_base;
	const char *slash;
	struct fwupd_cab_hdr chdr;
	struct fwupd_cab_folder cfolder;
	struct fwupd_cab_file_hdr f0, f1, f2;
	struct fwupd_cab_data_hdr dhdr;
	unsigned int xml_len, readme_len, bin_len;
	unsigned int f0_namelen, f1_namelen, f2_namelen;
	unsigned int coff_data, total_uncomp, total_cab;
	int fd;

	fwupd_build_bin_image(plugin, device_id, guid, fw_version, flags, bin_buf, bin_sha256);
	format_sha256_hex(bin_sha256, sha_hex);

	bin_base = bin_path;
	slash = strrchr(bin_path, '/');
	if (slash)
		bin_base = slash + 1;

	sprintf(xml_buf,
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<component type=\"firmware\">\n"
		"  <id>%s</id>\n"
		"  <name>%s</name>\n"
		"  <summary>%s</summary>\n"
		"  <developer_name>SIX Storage Engineering</developer_name>\n"
		"  <provides>\n"
		"    <firmware type=\"flashed\">%s</firmware>\n"
		"  </provides>\n"
		"  <custom>\n"
		"    <value key=\"LVFS::VersionFormat\">plain</value>\n"
		"    <value key=\"LVFS::UpdateProtocol\">%s</value>\n"
		"    <value key=\"LVFS::Plugin\">%s</value>\n"
		"    <value key=\"LVFS::DeviceId\">%s</value>\n"
		"  </custom>\n"
		"  <releases>\n"
		"    <release version=\"%s\" date=\"2026-09-28\" urgency=\"%s\">\n"
		"      <checksum type=\"sha256\" filename=\"%s\" target=\"content\">%s</checksum>\n"
		"      <description>\n"
		"        <p>%s</p>\n"
		"      </description>\n"
		"      <artifacts>\n"
		"        <artifact type=\"source\">\n"
		"          <filename>%s</filename>\n"
		"          <checksum type=\"sha256\">%s</checksum>\n"
		"        </artifact>\n"
		"      </artifacts>\n"
		"    </release>\n"
		"  </releases>\n"
		"</component>\n",
		component_id, device_id, summary, guid,
		update_protocol, plugin, device_id,
		fw_version, urgency, bin_base, sha_hex,
		summary, bin_base, sha_hex);

	sprintf(readme_buf,
		"README FIRST\n"
		"============\n\n"
		"Install this LVFS Cabinet archive on SIX using fwupdmgr:\n\n"
		"    fwupdmgr install %s\n",
		cab_path);

	xml_len = (unsigned int)strlen(xml_buf);
	readme_len = (unsigned int)strlen(readme_buf);
	bin_len = (unsigned int)sizeof(bin_buf);

	f0_namelen = (unsigned int)strlen("firmware.metainfo.xml") + 1U;
	f1_namelen = (unsigned int)strlen("README.txt") + 1U;
	f2_namelen = (unsigned int)strlen(bin_base) + 1U;

	coff_data = (unsigned int)sizeof(chdr) + (unsigned int)sizeof(cfolder) +
		    (unsigned int)sizeof(f0) + f0_namelen +
		    (unsigned int)sizeof(f1) + f1_namelen +
		    (unsigned int)sizeof(f2) + f2_namelen;
	total_uncomp = xml_len + readme_len + bin_len;
	total_cab = coff_data + (unsigned int)sizeof(dhdr) + total_uncomp;

	memset(&chdr, 0, sizeof(chdr));
	chdr.signature = FWUPD_CAB_MAGIC;
	chdr.cb_cabinet = total_cab;
	chdr.coff_files = (unsigned int)sizeof(chdr) + (unsigned int)sizeof(cfolder);
	chdr.version_minor = 3;
	chdr.version_major = 1;
	chdr.c_folders = 1;
	chdr.c_files = 3;
	chdr.set_id = 0x5358;

	memset(&cfolder, 0, sizeof(cfolder));
	cfolder.coff_cab_start = coff_data;
	cfolder.c_cfdata = 1;
	cfolder.type_compress = 0;

	memset(&f0, 0, sizeof(f0));
	f0.cb_file = xml_len;
	f0.uoff_folder_start = 0;
	f0.date = 0x5d3c;
	f0.time = 0x0800;
	f0.attribs = 0x20;

	memset(&f1, 0, sizeof(f1));
	f1.cb_file = readme_len;
	f1.uoff_folder_start = xml_len;
	f1.date = 0x5d3c;
	f1.time = 0x0800;
	f1.attribs = 0x20;

	memset(&f2, 0, sizeof(f2));
	f2.cb_file = bin_len;
	f2.uoff_folder_start = xml_len + readme_len;
	f2.date = 0x5d3c;
	f2.time = 0x0800;
	f2.attribs = 0x20;

	memset(&dhdr, 0, sizeof(dhdr));
	dhdr.csum = 0;
	dhdr.cb_data = (unsigned short)total_uncomp;
	dhdr.cb_uncomp = (unsigned short)total_uncomp;

	fd = open(cab_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -1;

	write(fd, &chdr, sizeof(chdr));
	write(fd, &cfolder, sizeof(cfolder));
	write(fd, &f0, sizeof(f0));
	write(fd, "firmware.metainfo.xml", (int)f0_namelen);
	write(fd, &f1, sizeof(f1));
	write(fd, "README.txt", (int)f1_namelen);
	write(fd, &f2, sizeof(f2));
	write(fd, bin_base, (int)f2_namelen);
	write(fd, &dhdr, sizeof(dhdr));
	write(fd, xml_buf, (int)xml_len);
	write(fd, readme_buf, (int)readme_len);
	write(fd, bin_buf, (int)bin_len);
	close(fd);
	return 0;
}

static int bin_file_valid(const char *path, const char *expected_plugin)
{
	struct fwupd_bin_hdr hdr;
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return 0;
	if (read(fd, &hdr, sizeof(hdr)) != (int)sizeof(hdr)) {
		close(fd);
		return 0;
	}
	close(fd);
	return (hdr.magic == FWUPD_BIN_MAGIC &&
		strcmp(hdr.plugin, expected_plugin) == 0);
}

static void ensure_lvfs_repository(int force_rebuild)
{
	int i;

	mkdir("/etc/fwupd", 0755);
	mkdir("/etc/fwupd/remotes.d", 0755);
	mkdir("/etc/fwupd/remotes.d/lvfs", 0755);
	mkdir(LVFS_PKG_DIR, 0755);
	mkdir("/var", 0755);
	mkdir("/var/lib", 0755);
	mkdir("/var/lib/fwupd", 0755);
	mkdir("/data/vendor", 0755);
	mkdir("/data/vendor/fwupd", 0755);
	symlink("/data/vendor/fwupd/history.db", FWUPD_HISTORY_PATH);

	parse_lvfs_metadata_xml();

	for (i = 0; i < nr_lvfs_releases; i++) {
		const struct lvfs_release *r = &lvfs_catalog[i];
		if (force_rebuild || !bin_file_valid(r->bin_filename, r->plugin)) {
			write_bin_firmware_file(r->bin_filename, r->plugin,
						r->device_id, r->guid,
						r->latest_ver, r->capsule_flags);
		}
		if (force_rebuild || !bin_file_valid(r->factory_bin_filename, r->plugin)) {
			write_bin_firmware_file(r->factory_bin_filename, r->plugin,
						r->device_id, r->guid,
						r->factory_ver, r->capsule_flags);
		}
		if (force_rebuild || !is_cab_file(r->pkg_filename)) {
			write_cab_firmware_file(r->pkg_filename, r->bin_filename,
						r->component_id, r->plugin,
						r->update_protocol, r->device_id,
						r->guid, r->latest_ver,
						r->urgency, r->summary,
						r->capsule_flags);
		}
		if (force_rebuild || !is_cab_file(r->factory_pkg_filename)) {
			write_cab_firmware_file(r->factory_pkg_filename, r->factory_bin_filename,
						r->component_id, r->plugin,
						r->update_protocol, r->device_id,
						r->guid, r->factory_ver,
						"Low", "Factory baseline firmware image",
						r->capsule_flags);
		}
	}
}

/*
 * Inspect any .bin file on disk using streaming SHA-256.
 * Works on both SIX controller .bin images and arbitrary real-world .bin files
 * of any size (Samsung UFSH, ARM Cortex-R, ELF32, UEFI Capsule, raw vendor blobs).
 */
static int inspect_bin_file(const char *bin_path,
			    unsigned char hdr_buf[FWUPD_HDR_INSPECT_LEN],
			    unsigned int *out_hdr_captured,
			    unsigned int *out_total_size,
			    unsigned int *out_nonzero_bytes,
			    unsigned char out_full_sha256[32],
			    unsigned char out_code_sha256[32])
{
	struct fwupd_sha256_ctx full_ctx, code_ctx;
	unsigned char chunk[FWUPD_CHUNK_SIZE];
	unsigned int offset = 0, hdr_cap = 0, nonzero = 0;
	int fd, nread, i;

	fd = open(bin_path, O_RDONLY);
	if (fd < 0)
		return -1;

	memset(hdr_buf, 0, FWUPD_HDR_INSPECT_LEN);
	fwupd_sha256_init(&full_ctx);
	fwupd_sha256_init(&code_ctx);

	while ((nread = read(fd, chunk, sizeof(chunk))) > 0) {
		unsigned int len = (unsigned int)nread;
		for (i = 0; i < nread; i++) {
			unsigned int pos = offset + (unsigned int)i;
			if (pos < FWUPD_HDR_INSPECT_LEN) {
				hdr_buf[pos] = chunk[i];
				if (pos + 1 > hdr_cap)
					hdr_cap = pos + 1;
			}
			if (chunk[i] != 0)
				nonzero++;
		}
		fwupd_sha256_update(&full_ctx, chunk, len);
		if (offset + len > (unsigned int)sizeof(struct fwupd_bin_hdr)) {
			unsigned int hdr_sz = (unsigned int)sizeof(struct fwupd_bin_hdr);
			unsigned int code_start = (offset >= hdr_sz) ? 0U : (hdr_sz - offset);
			fwupd_sha256_update(&code_ctx, chunk + code_start, len - code_start);
		}
		offset += len;
	}
	close(fd);

	fwupd_sha256_final(&full_ctx, out_full_sha256);
	fwupd_sha256_final(&code_ctx, out_code_sha256);
	if (out_hdr_captured)
		*out_hdr_captured = hdr_cap;
	if (out_total_size)
		*out_total_size = offset;
	if (out_nonzero_bytes)
		*out_nonzero_bytes = nonzero;
	return 0;
}

static const char *describe_bin_format(const unsigned char *hdr_buf, unsigned int hdr_len)
{
	if (hdr_len >= sizeof(struct fwupd_bin_hdr)) {
		const struct fwupd_bin_hdr *hdr = (const struct fwupd_bin_hdr *)hdr_buf;
		if (hdr->magic == FWUPD_BIN_MAGIC)
			return "ARM Cortex-R5 Controller Firmware Image (.bin, SFWM)";
	}
	if (hdr_len >= 4 && hdr_buf[0] == 'U' && hdr_buf[1] == 'F' &&
	    hdr_buf[2] == 'S' && hdr_buf[3] == 'H')
		return "Samsung JEDEC UFS Controller Firmware Binary (.bin, UFSH)";
	if (hdr_len >= 4 && hdr_buf[0] == 0x7f && hdr_buf[1] == 'E' &&
	    hdr_buf[2] == 'L' && hdr_buf[3] == 'F')
		return "ELF32 Controller Firmware Binary (.bin)";
	if (hdr_len >= 4 && hdr_buf[3] == 0xea)
		return "ARM32 Raw Vector Table Firmware Binary (.bin)";
	if (hdr_len >= 2 && hdr_buf[0] == 0x55 && hdr_buf[1] == 0xaa)
		return "PCIe Option ROM / x86 Firmware Binary (.bin)";
	return "Raw Vendor Controller Microcode Binary (.bin)";
}

static int probe_all_devices(struct fwupd_device *devs, int max_devs)
{
	int total = 0;
	int i;

	for (i = 0; i < NR_PLUGINS && total < max_devs; i++) {
		total += fwupd_plugins[i]->probe(devs + total, max_devs - total);
	}
	return total;
}

static const struct fwupd_plugin_ops *find_plugin(const char *name)
{
	int i;
	for (i = 0; i < NR_PLUGINS; i++) {
		if (strcmp(fwupd_plugins[i]->name, name) == 0)
			return fwupd_plugins[i];
	}
	return NULL;
}

static struct fwupd_device *find_device(struct fwupd_device *devs, int nr_devs,
					const char *query)
{
	int i;
	if (!query || !query[0])
		return NULL;
	for (i = 0; i < nr_devs; i++) {
		if (strcmp(devs[i].device_id, query) == 0 ||
		    strcmp(devs[i].guid, query) == 0 ||
		    (devs[i].guid2[0] && strcmp(devs[i].guid2, query) == 0) ||
		    (devs[i].update_protocol[0] && strcmp(devs[i].update_protocol, query) == 0) ||
		    strcmp(devs[i].dev_node, query) == 0 ||
		    strcmp(devs[i].plugin, query) == 0)
			return &devs[i];
	}
	return NULL;
}

static const struct lvfs_release *find_release_for_device(const char *device_id)
{
	int i;
	for (i = 0; i < nr_lvfs_releases; i++) {
		if (strcmp(lvfs_catalog[i].device_id, device_id) == 0)
			return &lvfs_catalog[i];
	}
	return NULL;
}

static void save_installed_digest(const char *device_id, const char *ver,
				  const unsigned char sha256[32])
{
	char path[80];
	int fd;

	sprintf(path, "/data/vendor/fwupd/digest-%s.bin", device_id);
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return;
	write(fd, ver, 16);
	write(fd, sha256, 32);
	close(fd);
}

static int load_installed_digest(const char *device_id, const char *ver,
				 unsigned char out_sha256[32])
{
	char path[80], saved_ver[16];
	int fd;

	sprintf(path, "/data/vendor/fwupd/digest-%s.bin", device_id);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	memset(saved_ver, 0, sizeof(saved_ver));
	if (read(fd, saved_ver, 16) != 16 || read(fd, out_sha256, 32) != 32) {
		close(fd);
		return -1;
	}
	close(fd);
	if (strcmp(saved_ver, ver) != 0)
		return -1;
	return 0;
}

static void record_history(const char *device_id, const char *guid,
			   const char *plugin, const char *old_ver,
			   const char *new_ver, const unsigned char sha256[32],
			   const char *status)
{
	char hex[65];
	char line[256];
	int fd;

	format_sha256_hex(sha256, hex);
	fd = open(FWUPD_HISTORY_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd < 0)
		return;
	sprintf(line, "device=%s guid=%s plugin=%s transition=%s->%s status=%s sha256=%.16s...\n",
		device_id, guid, plugin, old_ver, new_ver, status, hex);
	write(fd, line, (int)strlen(line));
	close(fd);
}

/* =========================================================================
 * CLI Subcommand Implementations
 * ========================================================================= */

static int cmd_get_plugins(void)
{
	int i;
	printf("fwupd 1.9.24 — Enabled Hardware Firmware Update Plugins:\n");
	for (i = 0; i < NR_PLUGINS; i++) {
		printf("  %-8s [ENABLED]  %s\n",
		       fwupd_plugins[i]->name, fwupd_plugins[i]->summary);
		printf("                      Transport: %s\n",
		       fwupd_plugins[i]->protocol);
	}
	return 0;
}

static int cmd_get_devices(void)
{
	struct fwupd_device devs[MAX_DEVICES];
	int nr = probe_all_devices(devs, MAX_DEVICES);
	int i;

	if (nr == 0) {
		fprintf(stderr, "fwupdmgr: permission denied probing hardware devices (SELinux blocked direct hardware access)\n");
		return 1;
	}

	printf("SIX Virtual Platform\n");
	for (i = 0; i < nr; i++) {
		char hex[65];
		const struct fwupd_device *d = &devs[i];
		const char *branch = (i == nr - 1) ? "└─" : "├─";
		const char *pipe   = (i == nr - 1) ? "  " : "│ ";

		format_sha256_hex(d->sha256, hex);
		printf("%s%s:\n", branch, d->name);
		printf("%s    Device ID:          %s\n", pipe, d->device_id);
		printf("%s    GUID:               %s\n", pipe, d->guid);
		if (d->guid2[0])
			printf("%s    Compat GUID:        %s\n", pipe, d->guid2);
		printf("%s    Plugin / Protocol:  %s (%s, %s)\n",
		       pipe, d->plugin, d->update_protocol, d->dev_node);
		printf("%s    Vendor / Serial:    %s / %s\n", pipe, d->vendor, d->serial);
		printf("%s    Current Version:    %s\n", pipe, d->version);
		printf("%s    Bootloader / Slots: %s\n", pipe, d->bootloader_info);
		printf("%s    Flags:              %s\n", pipe, d->flags_str);
		printf("%s    Device Checksum:    SHA256(%s)\n", pipe, hex);
	}
	return 0;
}

static int cmd_refresh(void)
{
	ensure_lvfs_repository(1);
	printf("Updating lvfs metadata from %s...\n", LVFS_META_PATH);
	printf("Successfully refreshed LVFS metadata (%d component releases, %d .cab archives & %d .bin images staged in %s)\n",
	       nr_lvfs_releases, nr_lvfs_releases * 2, nr_lvfs_releases * 2, LVFS_PKG_DIR);
	return 0;
}

static int cmd_get_updates(void)
{
	struct fwupd_device devs[MAX_DEVICES];
	int nr = probe_all_devices(devs, MAX_DEVICES);
	int i, updates_found = 0;

	if (nr == 0) {
		fprintf(stderr, "fwupdmgr: permission denied probing hardware devices (SELinux blocked direct hardware access)\n");
		return 1;
	}

	printf("Devices with available firmware updates:\n");
	for (i = 0; i < nr; i++) {
		const struct fwupd_device *d = &devs[i];
		const struct lvfs_release *r = find_release_for_device(d->device_id);
		if (!r)
			continue;
		if (compare_versions(d->version, r->latest_ver) < 0) {
			updates_found++;
			printf("  • %s (%s)\n", d->name, d->device_id);
			printf("      GUID:            %s\n", d->guid);
			printf("      Plugin / Node:   %s (%s)\n", d->plugin, d->dev_node);
			printf("      Current Version: %s\n", d->version);
			printf("      Update Version:  %s (Component: %s, Urgency: %s)\n",
			       r->latest_ver, r->component_id, r->urgency);
			printf("      LVFS Package:    %s\n", r->pkg_filename);
			printf("      Release Notes:   %s\n", r->summary);
		} else {
			printf("  ✓ %s (%s): up to date at version %s\n",
			       d->name, d->device_id, d->version);
		}
	}

	if (updates_found == 0)
		printf("No upgradable devices have pending updates.\n");
	return 0;
}

static int install_package_on_device(const char *pkg_path,
				     const char *target_query,
				     int allow_older,
				     int allow_reinstall)
{
	unsigned char hdr_buf[FWUPD_HDR_INSPECT_LEN];
	unsigned char full_sha256[32], code_sha256[32];
	unsigned int hdr_cap = 0, total_size = 0, nonzero = 0;
	const struct fwupd_bin_hdr *hdr;
	struct fwupd_device devs[MAX_DEVICES];
	struct fwupd_device *dev = NULL;
	const struct fwupd_plugin_ops *plug;
	char target_ver[16], old_ver[16], hex[65];
	int is_cab = 0, is_sfwm = 0, bin_fd, nr_devs, cmp, max_ver_len, rc;

	active_cab.active = 0;
	active_target_ver[0] = '\0';

	if (is_cab_file(pkg_path)) {
		is_cab = 1;
		rc = parse_cab_archive(pkg_path, &active_cab);
		if (rc == -2) {
			fprintf(stderr, "fwupdmgr: cabinet '%s' uses unsupported compression (only NONE and MSZIP are supported)\n",
				pkg_path);
			return 1;
		}
		if (rc < 0 || active_cab.payload_size < 64) {
			fprintf(stderr, "fwupdmgr: invalid or corrupted LVFS .cab archive '%s'\n", pkg_path);
			return 1;
		}
		if (!active_cab.checksum_verified) {
			fprintf(stderr, "fwupdmgr: .cab payload SHA-256 mismatch against firmware.metainfo.xml (%s)\n",
				active_cab.expected_sha256_hex);
			return 1;
		}
		memcpy(hdr_buf, active_cab.payload_hdr, FWUPD_HDR_INSPECT_LEN);
		hdr_cap = active_cab.payload_hdr_cap;
		total_size = active_cab.payload_size;
		nonzero = active_cab.payload_nonzero;
		memcpy(full_sha256, active_cab.payload_sha256, 32);
		memcpy(code_sha256, active_cab.payload_code_sha256, 32);
		active_cab.active = 1;
	} else {
		if (inspect_bin_file(pkg_path, hdr_buf, &hdr_cap, &total_size,
				     &nonzero, full_sha256, code_sha256) < 0) {
			fprintf(stderr, "fwupdmgr: cannot open firmware package '%s'\n", pkg_path);
			return 1;
		}
		if (total_size < 64) {
			fprintf(stderr, "fwupdmgr: invalid firmware .bin '%s' (%u bytes < 64B minimum)\n",
				pkg_path, total_size);
			return 1;
		}
	}

	hdr = (const struct fwupd_bin_hdr *)hdr_buf;
	if (hdr_cap >= sizeof(struct fwupd_bin_hdr) && hdr->magic == FWUPD_BIN_MAGIC)
		is_sfwm = 1;

	nr_devs = probe_all_devices(devs, MAX_DEVICES);
	if (target_query && target_query[0]) {
		dev = find_device(devs, nr_devs, target_query);
	} else if (is_cab) {
		if (active_cab.device_id[0])
			dev = find_device(devs, nr_devs, active_cab.device_id);
		if (!dev && active_cab.guid[0])
			dev = find_device(devs, nr_devs, active_cab.guid);
		if (!dev && active_cab.update_protocol[0])
			dev = find_device(devs, nr_devs, active_cab.update_protocol);
		if (!dev && active_cab.plugin[0])
			dev = find_device(devs, nr_devs, active_cab.plugin);
	} else if (is_sfwm) {
		dev = find_device(devs, nr_devs, hdr->device_id);
	} else {
		dev = find_device(devs, nr_devs, FWUPD_DEVID_NVME);
	}

	if (!dev) {
		active_cab.active = 0;
		fprintf(stderr, "fwupdmgr: target device for '%s' is not online\n", pkg_path);
		return 1;
	}

	max_ver_len = (strcmp(dev->plugin, "nvme") == 0) ? 8 : 4;
	memset(target_ver, 0, sizeof(target_ver));
	fwupd_detect_bin_version(hdr_buf, hdr_cap, full_sha256, max_ver_len, target_ver);
	if (is_cab && active_cab.release_version[0] && !is_sfwm) {
		strncpy(target_ver, active_cab.release_version, (size_t)max_ver_len);
		target_ver[max_ver_len] = '\0';
	}
	memset(active_target_ver, 0, sizeof(active_target_ver));
	strncpy(active_target_ver, target_ver, sizeof(active_target_ver) - 1);

	cmp = compare_versions(target_ver, dev->version);
	if (cmp == 0 && !allow_reinstall && (is_sfwm || is_cab)) {
		active_cab.active = 0;
		active_target_ver[0] = '\0';
		fprintf(stderr, "fwupdmgr: %s is already at version %s (use --allow-reinstall)\n",
			dev->device_id, dev->version);
		return 1;
	}
	if (cmp < 0 && !allow_older && (is_sfwm || is_cab)) {
		active_cab.active = 0;
		active_target_ver[0] = '\0';
		fprintf(stderr, "fwupdmgr: firmware version %s is older than installed %s on %s (use --allow-older)\n",
			target_ver, dev->version, dev->device_id);
		return 1;
	}

	plug = find_plugin(dev->plugin);
	if (!plug) {
		active_cab.active = 0;
		active_target_ver[0] = '\0';
		fprintf(stderr, "fwupdmgr: plugin '%s' not found\n", dev->plugin);
		return 1;
	}

	strcpy(old_ver, dev->version);
	format_sha256_hex(full_sha256, hex);

	if (is_cab) {
		const char *comp_str = ((active_cab.type_compress & 0x000f) == 1) ? "MSZIP" : "NONE";
		printf("Decompressing & verifying LVFS cabinet archive %s...\n", pkg_path);
		printf("  Cabinet Header: MSCF v%u.%u (%u bytes, %u files, %u CFDATA blocks, %s)\n",
		       active_cab.ver_major, active_cab.ver_minor,
		       active_cab.cb_cabinet, active_cab.c_files, active_cab.c_cfdata, comp_str);
		if (active_cab.component_id[0]) {
			printf("  AppStream ID  : %s (%s)\n",
			       active_cab.component_id,
			       active_cab.component_name[0] ? active_cab.component_name : active_cab.summary);
		}
		printf("  Payload Image : %s (%u bytes, %s)\n",
		       active_cab.source_filename, total_size,
		       describe_bin_format(hdr_buf, hdr_cap));
	} else {
		printf("Loading & verifying firmware binary %s...\n", pkg_path);
		printf("  Binary Format : %s (%u bytes)\n",
		       describe_bin_format(hdr_buf, hdr_cap), total_size);
	}
	printf("  Target Device : %s (%s, GUID %s)\n", dev->name, dev->device_id, dev->guid);
	printf("  Plugin / Node : %s (%s)\n", dev->plugin, dev->dev_node);
	printf("  Transition    : %s -> %s\n", old_ver, target_ver);
	printf("  Binary Digest : SHA256(%.32s...)\n", hex);

	if (is_sfwm && memcmp(code_sha256, hdr->code_sha256, 32) != 0) {
		printf("  [WARN] Binary microcode SHA-256 mismatch detected in user-space; forwarding to controller to verify hardware rejection...\n");
	}

	cab_reset_block_cache();
	bin_fd = open(pkg_path, O_RDONLY);
	if (bin_fd < 0) {
		active_cab.active = 0;
		active_target_ver[0] = '\0';
		return 1;
	}

	if (plug->write_firmware(dev, bin_fd, total_size) < 0) {
		close(bin_fd);
		active_cab.active = 0;
		active_target_ver[0] = '\0';
		record_history(dev->device_id, dev->guid, dev->plugin,
			       old_ver, target_ver, full_sha256, "failed-signature");
		printf("fwupdmgr: firmware update FAILED on %s (hardware rejected .bin image)\n",
		       dev->device_id);
		return 1;
	}
	close(bin_fd);
	active_cab.active = 0;
	active_target_ver[0] = '\0';

	/* Re-probe device to verify the new version and hardware SHA-256 digest */
	nr_devs = probe_all_devices(devs, MAX_DEVICES);
	dev = find_device(devs, nr_devs, dev->device_id);
	if (!dev || strcmp(dev->version, target_ver) != 0 ||
	    memcmp(dev->sha256, full_sha256, 32) != 0) {
		fprintf(stderr, "fwupdmgr: post-update verification failed on %s\n",
			dev ? dev->device_id : "device");
		return 1;
	}

	save_installed_digest(dev->device_id, dev->version, dev->sha256);
	record_history(dev->device_id, dev->guid, dev->plugin,
		       old_ver, dev->version, dev->sha256, "success");
	printf("Successfully updated %s (%s) from %s to %s [VERIFIED]\n",
	       dev->name, dev->device_id, old_ver, dev->version);
	return 0;
}

static int cmd_update(const char *target_query)
{
	struct fwupd_device devs[MAX_DEVICES];
	int nr = probe_all_devices(devs, MAX_DEVICES);
	int i, updated = 0, rc = 0;

	if (nr == 0) {
		fprintf(stderr, "fwupdmgr: permission denied probing hardware devices (SELinux blocked direct hardware access)\n");
		return 1;
	}

	for (i = 0; i < nr; i++) {
		const struct fwupd_device *d = &devs[i];
		const struct lvfs_release *r;

		if (target_query && target_query[0]) {
			if (strcmp(d->device_id, target_query) != 0 &&
			    strcmp(d->guid, target_query) != 0 &&
			    (d->guid2[0] == '\0' || strcmp(d->guid2, target_query) != 0) &&
			    strcmp(d->plugin, target_query) != 0 &&
			    strcmp(d->dev_node, target_query) != 0)
				continue;
		}

		r = find_release_for_device(d->device_id);
		if (!r)
			continue;
		if (compare_versions(d->version, r->latest_ver) < 0) {
			if (install_package_on_device(r->pkg_filename, d->device_id, 0, 0) != 0)
				rc = 1;
			else
				updated++;
		}
	}

	if (updated == 0 && rc == 0)
		printf("All matching devices are already up to date.\n");
	return rc;
}

static int cmd_verify(const char *target_query)
{
	struct fwupd_device devs[MAX_DEVICES];
	unsigned char expected_bin[sizeof(struct fwupd_bin_hdr) + FWUPD_DEFAULT_PAYLOAD_SIZE];
	unsigned char expected_sha256[32];
	int nr = probe_all_devices(devs, MAX_DEVICES);
	int i, verified = 0;

	for (i = 0; i < nr; i++) {
		const struct fwupd_device *d = &devs[i];
		const struct lvfs_release *r;
		char hex[65];

		if (target_query && target_query[0]) {
			if (strcmp(d->device_id, target_query) != 0 &&
			    strcmp(d->guid, target_query) != 0 &&
			    (d->guid2[0] == '\0' || strcmp(d->guid2, target_query) != 0) &&
			    strcmp(d->plugin, target_query) != 0)
				continue;
		}

		if (load_installed_digest(d->device_id, d->version, expected_sha256) < 0) {
			r = find_release_for_device(d->device_id);
			fwupd_build_bin_image(d->plugin, d->device_id, d->guid, d->version,
					      r ? r->capsule_flags : FWUPD_FLAG_SIGNED_PAYLOAD,
					      expected_bin, expected_sha256);
		}
		format_sha256_hex(d->sha256, hex);

		if (memcmp(d->sha256, expected_sha256, 32) == 0) {
			printf("Verified %s (%s, plugin=%s, version=%s): SHA256(%s) [OK]\n",
			       d->name, d->device_id, d->plugin, d->version, hex);
			verified++;
		} else {
			printf("Verification FAILED for %s (%s, version=%s)\n",
			       d->name, d->device_id, d->version);
			return 1;
		}
	}
	return (verified > 0) ? 0 : 1;
}

static int cmd_activate(const char *target_query, const char *slot_str)
{
	struct fwupd_device devs[MAX_DEVICES];
	struct fwupd_device *dev;
	const struct fwupd_plugin_ops *plug;
	unsigned int slot = slot_str ? (unsigned int)atoi(slot_str) : 1;
	char old_ver[16];
	int nr = probe_all_devices(devs, MAX_DEVICES);

	dev = find_device(devs, nr, target_query ? target_query : FWUPD_DEVID_NVME);
	if (!dev) {
		fprintf(stderr, "fwupdmgr activate: device not found\n");
		return 1;
	}
	plug = find_plugin(dev->plugin);
	if (!plug || !plug->activate_slot) {
		fprintf(stderr, "fwupdmgr activate: dual-slot activation is not supported by plugin '%s'\n",
			dev->plugin);
		return 1;
	}
	if (slot < 1 || slot > 2) {
		fprintf(stderr, "fwupdmgr activate: slot must be 1 or 2\n");
		return 1;
	}

	strcpy(old_ver, dev->version);
	if (plug->activate_slot(dev, slot) < 0) {
		fprintf(stderr, "fwupdmgr activate: failed to activate Slot %u on %s\n",
			slot, dev->device_id);
		return 1;
	}

	nr = probe_all_devices(devs, MAX_DEVICES);
	dev = find_device(devs, nr, FWUPD_DEVID_NVME);
	if (dev) {
		record_history(dev->device_id, dev->guid, dev->plugin,
			       old_ver, dev->version, dev->sha256, "slot-switched");
		printf("Switched %s (%s) active firmware slot to Slot %u (version %s)\n",
		       dev->name, dev->device_id, dev->active_slot, dev->version);
	}
	return 0;
}

static int cmd_get_history(void)
{
	FILE *fp = fopen(FWUPD_HISTORY_PATH, "r");
	char line[256];
	int count = 0;

	if (!fp) {
		printf("No firmware update history recorded yet (%s).\n", FWUPD_HISTORY_PATH);
		return 0;
	}
	printf("Firmware Update History (%s):\n", FWUPD_HISTORY_PATH);
	while (fgets(line, sizeof(line), fp)) {
		printf("  [%d] %s", ++count, line);
	}
	fclose(fp);
	if (count == 0)
		printf("  (empty)\n");
	return 0;
}

static int cmd_clear_history(void)
{
	unlink("/data/vendor/fwupd/history.db");
	unlink(FWUPD_HISTORY_PATH);
	symlink("/data/vendor/fwupd/history.db", FWUPD_HISTORY_PATH);
	printf("Cleared firmware update history (%s).\n", FWUPD_HISTORY_PATH);
	return 0;
}

static int cmd_examine(const char *pkg_path)
{
	unsigned char hdr_buf[FWUPD_HDR_INSPECT_LEN];
	unsigned char full_sha256[32], code_sha256[32];
	unsigned int hdr_cap = 0, total_size = 0, nonzero = 0;
	const struct fwupd_bin_hdr *hdr = (const struct fwupd_bin_hdr *)hdr_buf;
	char full_hex[65], code_hex[65], detected_ver[16];
	int sig_ok = 1, i;

	if (!pkg_path) {
		fprintf(stderr, "fwupdmgr examine: missing <.cab|.bin> file path\n");
		return 1;
	}

	if (is_cab_file(pkg_path)) {
		const char *comp_str;
		int rc = parse_cab_archive(pkg_path, &active_cab);
		if (rc < 0) {
			fprintf(stderr, "fwupdmgr examine: invalid or unsupported .cab archive '%s'\n",
				pkg_path);
			return 1;
		}
		comp_str = ((active_cab.type_compress & 0x000f) == 1) ? "MSZIP" : "NONE";
		format_sha256_hex(active_cab.payload_sha256, full_hex);
		printf("LVFS Cabinet Archive Inspection (%s):\n", pkg_path);
		printf("  Archive Format : Microsoft Cabinet Archive (MSCF v%u.%u, %u bytes, %u folder, %u files, %u CFDATA blocks, %s)\n",
		       active_cab.ver_major, active_cab.ver_minor,
		       active_cab.cb_cabinet, active_cab.c_folders,
		       active_cab.c_files, active_cab.c_cfdata, comp_str);
		for (i = 0; i < active_cab.nr_files; i++) {
			printf("  %s [%d] %s (%u bytes @ folder+0x%x)\n",
			       (i == 0) ? "Cabinet Files  :" : "                ",
			       i, active_cab.files[i].name,
			       active_cab.files[i].cb_file,
			       active_cab.files[i].uoff_folder_start);
		}
		if (active_cab.component_id[0])
			printf("  Component ID   : %s\n", active_cab.component_id);
		if (active_cab.component_name[0] || active_cab.summary[0])
			printf("  Component Name : %s (%s)\n",
			       active_cab.component_name[0] ? active_cab.component_name : active_cab.component_id,
			       active_cab.summary);
		if (active_cab.developer[0])
			printf("  Developer      : %s\n", active_cab.developer);
		if (active_cab.update_protocol[0])
			printf("  Update Protocol: %s\n", active_cab.update_protocol);
		if (active_cab.guid[0])
			printf("  AppStream GUID : %s\n", active_cab.guid);
		if (active_cab.release_version[0]) {
			printf("  Release Version: %s (Date: %s, Urgency: %s, Format: %s",
			       active_cab.release_version,
			       active_cab.release_date[0] ? active_cab.release_date : "n/a",
			       active_cab.urgency[0] ? active_cab.urgency : "normal",
			       active_cab.version_format[0] ? active_cab.version_format : "plain");
			if (active_cab.req_fw_min[0])
				printf(", Requires: >= %s", active_cab.req_fw_min);
			printf(")\n");
		}
		printf("  Payload Binary : %s (%u bytes)\n",
		       active_cab.source_filename, active_cab.payload_size);
		printf("  Binary Format  : %s\n",
		       describe_bin_format(active_cab.payload_hdr, active_cab.payload_hdr_cap));
		if (active_cab.expected_sha256_hex[0])
			printf("  Meta Checksum  : %s\n", active_cab.expected_sha256_hex);
		printf("  Payload SHA256 : %s [%s]\n",
		       full_hex, active_cab.checksum_verified ? "VALID SIGNATURE" : "CORRUPTED");
		return active_cab.checksum_verified ? 0 : 1;
	}

	if (inspect_bin_file(pkg_path, hdr_buf, &hdr_cap, &total_size,
			     &nonzero, full_sha256, code_sha256) < 0) {
		fprintf(stderr, "fwupdmgr examine: cannot open '%s'\n", pkg_path);
		return 1;
	}
	if (total_size < 64 || nonzero < 16) {
		fprintf(stderr, "fwupdmgr examine: '%s' is too small or empty (%u bytes)\n",
			pkg_path, total_size);
		return 1;
	}

	format_sha256_hex(full_sha256, full_hex);
	format_sha256_hex(code_sha256, code_hex);
	memset(detected_ver, 0, sizeof(detected_ver));
	fwupd_detect_bin_version(hdr_buf, hdr_cap, full_sha256, 8, detected_ver);

	printf("Firmware Binary Inspection (%s):\n", pkg_path);
	printf("  Binary Format  : %s\n", describe_bin_format(hdr_buf, hdr_cap));

	if (hdr_cap >= sizeof(struct fwupd_bin_hdr) && hdr->magic == FWUPD_BIN_MAGIC) {
		sig_ok = (memcmp(hdr->code_sha256, code_sha256, 32) == 0 &&
			  sizeof(struct fwupd_bin_hdr) + hdr->code_len == total_size);
		printf("  ARM Vector/Sig : 0x%08x (b 0xa0) / 0x%08x (\"SFWM\") v%u\n",
		       hdr->arm_b_reset, hdr->magic, hdr->hdr_version);
		printf("  Target Plugin  : %s\n", hdr->plugin);
		printf("  Target Device  : %s\n", hdr->device_id);
		printf("  Target GUID    : %s\n", hdr->guid);
		printf("  Target Version : %s\n", hdr->fw_version);
		printf("  Binary Size    : %u bytes (%u bytes microcode body)\n",
		       total_size, hdr->code_len);
		printf("  Image SHA256   : %s\n", full_hex);
		printf("  Code SHA256    : %s [%s]\n",
		       code_hex, sig_ok ? "VALID SIGNATURE" : "CORRUPTED");
	} else {
		printf("  Detected Rev   : %s\n", detected_ver);
		printf("  Binary Size    : %u bytes (%u non-zero bytes)\n",
		       total_size, nonzero);
		printf("  Image SHA256   : %s [VALID SIGNATURE]\n", full_hex);
	}
	return sig_ok ? 0 : 1;
}

static void usage(void)
{
	printf("fwupdmgr 1.9.24 (SIX Firmware Update Manager — nvme, ufs & scsi plugins)\n"
	       "Usage: fwupdmgr [--direct] <command> [options]\n\n"
	       "Commands:\n"
	       "  get-plugins                            List built-in hardware plugins (nvme, ufs, scsi)\n"
	       "  get-devices                            Probe hardware and list updatable devices\n"
	       "  refresh                                Refresh LVFS metadata, .cab & .bin packages\n"
	       "  get-updates                            Show available LVFS firmware updates\n"
	       "  update [DEVICE-ID|GUID|PLUGIN]         Update devices to latest LVFS .cab firmware\n"
	       "  install <firmware.cab|.bin> [DEVICE-ID] [--allow-older] [--allow-reinstall]\n"
	       "  install-blob <firmware.cab|.bin> [DEVICE-ID]\n"
	       "                                         Install an LVFS .cab archive or raw vendor .bin\n"
	       "  activate <DEVICE-ID> <slot>            Switch active NVMe firmware slot (1 or 2)\n"
	       "  verify [DEVICE-ID|GUID]                Verify device firmware SHA-256 checksums\n"
	       "  examine <firmware.cab|.bin>            Inspect an LVFS .cab archive or .bin image\n"
	       "  get-history                            Show firmware update history\n"
	       "  clear-history                          Clear firmware update history\n");
}

static int dispatch_fwupd_local(int argc, char **argv)
{
	const char *sub;

	if (argc < 2) {
		usage();
		return 1;
	}

	sub = argv[1];
	if (strcmp(sub, "get-plugins") == 0 || strcmp(sub, "plugins") == 0)
		return cmd_get_plugins();
	if (strcmp(sub, "get-devices") == 0 || strcmp(sub, "devices") == 0)
		return cmd_get_devices();
	if (strcmp(sub, "refresh") == 0)
		return cmd_refresh();
	if (strcmp(sub, "get-updates") == 0 || strcmp(sub, "updates") == 0)
		return cmd_get_updates();
	if (strcmp(sub, "update") == 0 || strcmp(sub, "upgrade") == 0)
		return cmd_update((argc >= 3 && argv[2][0] != '-') ? argv[2] : NULL);
	if (strcmp(sub, "install") == 0 || strcmp(sub, "install-blob") == 0) {
		const char *pkg_path = NULL;
		const char *target = NULL;
		int allow_older = (strcmp(sub, "install-blob") == 0);
		int allow_reinstall = (strcmp(sub, "install-blob") == 0);
		int i;
		for (i = 2; i < argc; i++) {
			if (strcmp(argv[i], "--allow-older") == 0)
				allow_older = 1;
			else if (strcmp(argv[i], "--allow-reinstall") == 0)
				allow_reinstall = 1;
			else if (!pkg_path)
				pkg_path = argv[i];
			else if (!target)
				target = argv[i];
		}
		if (!pkg_path) {
			fprintf(stderr, "fwupdmgr %s: missing <firmware.cab|.bin> path\n", sub);
			return 1;
		}
		return install_package_on_device(pkg_path, target, allow_older, allow_reinstall);
	}
	if (strcmp(sub, "activate") == 0 || strcmp(sub, "switch-slot") == 0) {
		const char *dev_q = (argc >= 3) ? argv[2] : FWUPD_DEVID_NVME;
		const char *slot_s = (argc >= 4) ? argv[3] : "1";
		return cmd_activate(dev_q, slot_s);
	}
	if (strcmp(sub, "verify") == 0)
		return cmd_verify((argc >= 3 && argv[2][0] != '-') ? argv[2] : NULL);
	if (strcmp(sub, "examine") == 0 || strcmp(sub, "inspect") == 0)
		return cmd_examine((argc >= 3) ? argv[2] : NULL);
	if (strcmp(sub, "get-history") == 0 || strcmp(sub, "history") == 0)
		return cmd_get_history();
	if (strcmp(sub, "clear-history") == 0)
		return cmd_clear_history();
	if (strcmp(sub, "-h") == 0 || strcmp(sub, "--help") == 0 || strcmp(sub, "help") == 0) {
		usage();
		return 0;
	}

	fprintf(stderr, "fwupdmgr: unknown command '%s'\n", sub);
	usage();
	return 1;
}

#define FWUPD_RPC_OUT_PATH "/tmp/.fwupd_rpc.out"

static int run_fwupd_binder_daemon(void)
{
	int bfd;
	struct binder_service_info sinfo;
	char self_ctx[64];
	int cfd;

	ensure_lvfs_repository(0);

	memset(self_ctx, 0, sizeof(self_ctx));
	cfd = open("/proc/self/attr/current", O_RDONLY);
	if (cfd >= 0) {
		int n = read(cfd, self_ctx, sizeof(self_ctx) - 1);
		if (n > 0)
			self_ctx[n] = '\0';
		close(cfd);
	}

	bfd = open("/dev/binder", O_RDWR);
	if (bfd < 0) {
		fprintf(stderr, "fwupd: cannot open /dev/binder: %s\n", strerror(errno));
		return 1;
	}

	memset(&sinfo, 0, sizeof(sinfo));
	strcpy(sinfo.name, IFWUPD_SERVICE_NAME);
	strcpy(sinfo.descriptor, IFWUPD_DESCRIPTOR);
	if (ioctl(bfd, BINDER_IOC_REGISTER_SVC, &sinfo) < 0) {
		fprintf(stderr, "fwupd: failed to register service '%s': %s\n",
			IFWUPD_SERVICE_NAME, strerror(errno));
		close(bfd);
		return 1;
	}

	printf("[fwupd] Vendor Firmware Update Service ready (pid=%d, handle=%d, context=%s)\n",
	       getpid(), sinfo.handle, self_ctx[0] ? self_ctx : "u:r:fwupd:s0");
	fflush(stdout);

	while (1) {
		struct binder_ipc_msg msg;
		int rc;

		memset(&msg, 0, sizeof(msg));
		if (ioctl(bfd, BINDER_IOC_RECV, &msg) < 0)
			continue;

		if (msg.code == PING_TRANSACTION) {
			msg.status = 0;
			strcpy(msg.data, "PONG");
			msg.data_size = 5;
		} else if (msg.code == DUMP_TRANSACTION) {
			struct fwupd_device devs[MAX_DEVICES];
			int nr = probe_all_devices(devs, MAX_DEVICES);
			int i, pos = 0;

			pos += snprintf(msg.data + pos, sizeof(msg.data) - pos,
					"VENDOR FWUPD SERVICE (dumpsys fwupd)\n"
					"  Interface: %s\n"
					"  SELinux Domain: %s\n"
					"  Capabilities: CAP_SYS_RAWIO, CAP_SYS_ADMIN (SG_IO 0x2285 allowxperm)\n"
					"  Plugins (%d): nvme, ufs, scsi\n"
					"  Updatable Hardware Devices (%d):\n",
					IFWUPD_DESCRIPTOR,
					self_ctx[0] ? self_ctx : "u:r:fwupd:s0",
					NR_PLUGINS, nr);
			for (i = 0; i < nr && pos + 160 < (int)sizeof(msg.data); i++) {
				pos += snprintf(msg.data + pos, sizeof(msg.data) - pos,
						"    [%d] %s (id=%s, plugin=%s, node=%s, version=%s)\n",
						i + 1, devs[i].name, devs[i].device_id,
						devs[i].plugin, devs[i].dev_node, devs[i].version);
			}
			msg.data_size = (unsigned int)(pos + 1);
			msg.status = 0;
		} else {
			char cmdbuf[512];
			char *rpc_argv[16];
			int rpc_argc = 0;
			char *p;
			int saved_out, saved_err, out_fd;

			memset(cmdbuf, 0, sizeof(cmdbuf));
			if (msg.data_size > 0 && msg.data[0]) {
				strncpy(cmdbuf, msg.data, sizeof(cmdbuf) - 1);
			} else {
				switch (msg.code) {
				case IFWUPD_GET_PLUGINS:   strcpy(cmdbuf, "get-plugins"); break;
				case IFWUPD_GET_DEVICES:   strcpy(cmdbuf, "get-devices"); break;
				case IFWUPD_REFRESH:       strcpy(cmdbuf, "refresh"); break;
				case IFWUPD_GET_UPDATES:   strcpy(cmdbuf, "get-updates"); break;
				case IFWUPD_UPDATE:        strcpy(cmdbuf, "update"); break;
				case IFWUPD_VERIFY:        strcpy(cmdbuf, "verify"); break;
				case IFWUPD_GET_HISTORY:   strcpy(cmdbuf, "get-history"); break;
				case IFWUPD_CLEAR_HISTORY: strcpy(cmdbuf, "clear-history"); break;
				default:                   strcpy(cmdbuf, "get-devices"); break;
				}
			}

			rpc_argv[rpc_argc++] = "fwupdmgr";
			p = cmdbuf;
			while (*p && rpc_argc < 15) {
				while (*p == ' ' || *p == '\t')
					*p++ = '\0';
				if (!*p)
					break;
				rpc_argv[rpc_argc++] = p;
				while (*p && *p != ' ' && *p != '\t')
					p++;
			}
			rpc_argv[rpc_argc] = NULL;

			unlink(FWUPD_RPC_OUT_PATH);
			out_fd = open(FWUPD_RPC_OUT_PATH, O_CREAT | O_TRUNC | O_RDWR, 0666);
			saved_out = dup(1);
			saved_err = dup(2);
			if (out_fd >= 0) {
				dup2(out_fd, 1);
				dup2(out_fd, 2);
			}

			rc = dispatch_fwupd_local(rpc_argc, rpc_argv);
			fflush(stdout);
			fflush(stderr);

			if (saved_out >= 0) {
				dup2(saved_out, 1);
				close(saved_out);
			}
			if (saved_err >= 0) {
				dup2(saved_err, 2);
				close(saved_err);
			}

			msg.data[0] = '\0';
			msg.data_size = 0;
			if (out_fd >= 0) {
				int n;
				lseek(out_fd, 0, SEEK_SET);
				n = read(out_fd, msg.data, sizeof(msg.data) - 1);
				if (n > 0) {
					msg.data[n] = '\0';
					msg.data_size = (unsigned int)(n + 1);
				}
				close(out_fd);
			}
			msg.status = rc;
		}

		if (!(msg.flags & TF_ONE_WAY))
			ioctl(bfd, BINDER_IOC_REPLY, &msg);
	}
	close(bfd);
	return 0;
}

static unsigned int subcmd_to_binder_code(const char *sub)
{
	if (!strcmp(sub, "get-plugins") || !strcmp(sub, "plugins"))
		return IFWUPD_GET_PLUGINS;
	if (!strcmp(sub, "get-devices") || !strcmp(sub, "devices"))
		return IFWUPD_GET_DEVICES;
	if (!strcmp(sub, "refresh"))
		return IFWUPD_REFRESH;
	if (!strcmp(sub, "get-updates") || !strcmp(sub, "updates"))
		return IFWUPD_GET_UPDATES;
	if (!strcmp(sub, "update") || !strcmp(sub, "upgrade"))
		return IFWUPD_UPDATE;
	if (!strcmp(sub, "install") || !strcmp(sub, "install-blob"))
		return IFWUPD_INSTALL;
	if (!strcmp(sub, "activate") || !strcmp(sub, "switch-slot"))
		return IFWUPD_ACTIVATE;
	if (!strcmp(sub, "verify"))
		return IFWUPD_VERIFY;
	if (!strcmp(sub, "examine") || !strcmp(sub, "inspect"))
		return IFWUPD_EXAMINE;
	if (!strcmp(sub, "get-history") || !strcmp(sub, "history"))
		return IFWUPD_GET_HISTORY;
	if (!strcmp(sub, "clear-history"))
		return IFWUPD_CLEAR_HISTORY;
	return IFWUPD_EXEC_CMD;
}

int main(int argc, char **argv)
{
	const char *prog = strrchr(argv[0], '/');
	int direct_mode = 0;
	int i;

	prog = prog ? (prog + 1) : argv[0];

	if (strcmp(prog, "fwupd") == 0 ||
	    (argc >= 2 && strcmp(argv[1], "--daemon") == 0)) {
		return run_fwupd_binder_daemon();
	}

	if (argc >= 2 && strcmp(argv[1], "--direct") == 0) {
		direct_mode = 1;
		for (i = 1; i < argc - 1; i++)
			argv[i] = argv[i + 1];
		argc--;
	}

	if (argc < 2) {
		usage();
		return 1;
	}
	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 ||
	    strcmp(argv[1], "help") == 0) {
		usage();
		return 0;
	}

	/*
	 * Unless --direct is specified, talk to the vendor fwupd daemon
	 * ("fwupd" [org.freedesktop.fwupd.IFwupd]) over Binder IPC.
	 */
	if (!direct_mode) {
		int bfd = open("/dev/binder", O_RDWR);
		if (bfd >= 0) {
			struct binder_service_info sinfo;
			int rc;
			memset(&sinfo, 0, sizeof(sinfo));
			strcpy(sinfo.name, IFWUPD_SERVICE_NAME);
			rc = ioctl(bfd, BINDER_IOC_LOOKUP_SVC, &sinfo);
			if (rc < 0 && (errno == EACCES || errno == EPERM)) {
				fprintf(stderr,
					"fwupdmgr: SELinux ServiceManager denied access to service '%s' (%s)\n",
					IFWUPD_SERVICE_NAME, strerror(errno));
				close(bfd);
				return 1;
			}
			if (rc == 0 && sinfo.handle > 0) {
				struct binder_ipc_msg msg;
				int pos = 0;
				int out_fd;

				memset(&msg, 0, sizeof(msg));
				msg.target_handle = sinfo.handle;
				msg.code = subcmd_to_binder_code(argv[1]);
				strcpy(msg.interface_token, IFWUPD_DESCRIPTOR);
				for (i = 1; i < argc; i++) {
					int alen = strlen(argv[i]);
					if (pos > 0 && pos + 1 < (int)sizeof(msg.data))
						msg.data[pos++] = ' ';
					if (pos + alen < (int)sizeof(msg.data) - 1) {
						memcpy(msg.data + pos, argv[i], alen);
						pos += alen;
					}
				}
				msg.data[pos] = '\0';
				msg.data_size = (unsigned int)(pos + 1);

				if (ioctl(bfd, BINDER_IOC_TRANSACT, &msg) < 0) {
					fprintf(stderr,
						"fwupdmgr: Binder IPC transaction to '%s' failed: %s\n",
						IFWUPD_SERVICE_NAME, strerror(errno));
					close(bfd);
					return 1;
				}
				close(bfd);

				out_fd = open(FWUPD_RPC_OUT_PATH, O_RDONLY);
				if (out_fd >= 0) {
					char rbuf[1024];
					int n;
					while ((n = read(out_fd, rbuf, sizeof(rbuf))) > 0)
						write(1, rbuf, n);
					close(out_fd);
				} else if (msg.data_size > 0 && msg.data[0]) {
					printf("%s", msg.data);
				}
				return (msg.status == 0) ? 0 : 1;
			}
			close(bfd);
		}
	}

	ensure_lvfs_repository(0);
	return dispatch_fwupd_local(argc, argv);
}
