/*
 *  linux/drivers/block/nvme.c
 *
 *  NVM Express (NVMe 1.4) Controller & Namespace Driver for SIX.
 *
 *  Exposes:
 *    - /dev/nvme0     (char major 59, minor 0): NVMe Admin controller device
 *    - /dev/nvme0n1   (block major 63, minor 0): NVMe Namespace 1 block device
 *    - /dev/nvme0n1p1 (block major 63, minor 1): NVMe Namespace 1 partition 1
 *    - /proc/nvme     (PROC_NVME): Controller registers, SQ/CQ rings & SMART
 *
 *  Architecture:
 *    - Implements real NVMe 64-byte Submission Queue Entries (SQEs) and
 *      16-byte Completion Queue Entries (CQEs) with Phase Tag toggling and
 *      SQ Tail / CQ Head doorbell emulation across Admin Queue (QID 0, depth 32)
 *      and I/O Queue (QID 1, depth 64).
 *    - Transfers PRP buffers directly between guest RAM and the host backing
 *      image (./disk/x86/nvme0n1.img, mounted at /data as Android userdata).
 */

#define MAJOR_NR NVME_MAJOR

#include <solaris.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/genhd.h>
#include <linux/string.h>
#include <linux/blk.h>
#include <linux/nvme.h>
#include <linux/fwupd.h>
#include <linux/selinux.h>
#include <asm/segment.h>
#include <asm/system.h>

#define NVME_MAX_MINORS		4

struct nvme_queue {
	unsigned short		qid;
	unsigned short		q_depth;
	unsigned short		sq_head;
	unsigned short		sq_tail;
	unsigned short		cq_head;
	unsigned short		cq_tail;
	unsigned char		cq_phase;
	unsigned char		cq_expected_phase;
	unsigned short		next_cid;
	unsigned long		sq_doorbell_writes;
	unsigned long		cq_doorbell_writes;
	unsigned long		completed_cmds;
	struct nvme_command	sq[NVME_IOQ_DEPTH];
	struct nvme_completion	cq[NVME_IOQ_DEPTH];
};

static int nvme_fd = -1;
static int nvme_online = 0;
static unsigned long nvme_sectors = NVME_DEFAULT_SECTORS;
static unsigned long nvme_base_offset = SIX_PART_NVME_OFFSET;
static char nvme_img_path[128] = NVMEDISKFILE;

static struct nvme_queue nvme_admin_q;
static struct nvme_queue nvme_io_q;

/* Controller registers (simulated BAR0 MMIO) */
static unsigned int nvme_reg_cap_lo = 0x0020003fU; /* MQES=63 (64 entries), CQR=1, TO=32 */
static unsigned int nvme_reg_cap_hi = 0x00000020U; /* DSTRD=0, NSSRS=1, CSS=NVM */
static unsigned int nvme_reg_vs     = NVME_VS(1, 4, 0);
static unsigned int nvme_reg_cc     = 0x00460001U; /* EN=1, IOSQES=6 (64B), IOCQES=4 (16B) */
static unsigned int nvme_reg_csts   = 0x00000001U; /* RDY=1 */

/* SMART / Health telemetry counters */
static unsigned long smart_host_reads = 0;
static unsigned long smart_host_writes = 0;
static unsigned long smart_sectors_read = 0;
static unsigned long smart_sectors_written = 0;
static unsigned long smart_flush_cmds = 0;
static unsigned long smart_dsm_cmds = 0;
static unsigned long smart_dsm_sectors_trimmed = 0;
static unsigned long smart_media_errors = 0;
static unsigned long smart_power_cycles = 1;

/* NVMe Firmware Slot (LID 0x03) & Firmware Image Download (0x11) state */
#define NVME_HDR_MAGIC		0x454d5632U	/* "NVM2" (.bin full SHA-256) */
#define NVME_PERSIST_HDR_OFFSET	0UL		/* Sector 0 (before ext4 superblock at 1024B) */

struct nvme_persist_hdr {
	unsigned int	magic;
	unsigned char	active_fw_slot;
	unsigned char	rsvd0[3];
	char		fw_slots[2][8];
	unsigned char	fw_slot_sha256[2][32];
	unsigned int	fw_commit_count;
	unsigned int	power_cycles;
	unsigned char	rsvd[416];
};

static struct nvme_persist_hdr nvme_hdr;
static struct fwupd_stream_state nvme_fw_stream;
static unsigned char nvme_active_fw_slot = 1;
static char nvme_fw_slots[2][8] = {
	{ '1', '.', '4', '.', '0', ' ', ' ', ' ' },
	{ ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' }
};
static unsigned char nvme_fw_slot_sha256[2][32];
static unsigned int nvme_fw_commit_count = 0;

static int nvme_sizes[NVME_MAX_MINORS];
static int nvme_blocksizes[NVME_MAX_MINORS];
static struct hd_struct nvme_part[NVME_MAX_MINORS];

static unsigned char nvme_zero_page[4096];
static unsigned char nvme_admin_bounce[4096];

static void nvme_save_persist_hdr(void)
{
	if (nvme_fd < 0)
		return;
	memset(&nvme_hdr, 0, sizeof(nvme_hdr));
	nvme_hdr.magic = NVME_HDR_MAGIC;
	nvme_hdr.active_fw_slot = nvme_active_fw_slot;
	memcpy(nvme_hdr.fw_slots, nvme_fw_slots, sizeof(nvme_fw_slots));
	memcpy(nvme_hdr.fw_slot_sha256, nvme_fw_slot_sha256, sizeof(nvme_fw_slot_sha256));
	nvme_hdr.fw_commit_count = nvme_fw_commit_count;
	nvme_hdr.power_cycles = (unsigned int)smart_power_cycles;
	lseek(nvme_fd, (long)(nvme_base_offset + NVME_PERSIST_HDR_OFFSET), 0);
	write(nvme_fd, &nvme_hdr, sizeof(nvme_hdr));
}

static void nvme_load_or_init_persist_hdr(void)
{
	if (nvme_fd < 0)
		return;

	memset(&nvme_hdr, 0, sizeof(nvme_hdr));
	lseek(nvme_fd, (long)(nvme_base_offset + NVME_PERSIST_HDR_OFFSET), 0);
	if (read(nvme_fd, &nvme_hdr, sizeof(nvme_hdr)) == (int)sizeof(nvme_hdr) &&
	    nvme_hdr.magic == NVME_HDR_MAGIC) {
		if (nvme_hdr.active_fw_slot < 1 || nvme_hdr.active_fw_slot > 2)
			nvme_hdr.active_fw_slot = 1;
		nvme_active_fw_slot = nvme_hdr.active_fw_slot;
		memcpy(nvme_fw_slots, nvme_hdr.fw_slots, sizeof(nvme_fw_slots));
		memcpy(nvme_fw_slot_sha256, nvme_hdr.fw_slot_sha256, sizeof(nvme_fw_slot_sha256));
		nvme_fw_commit_count = nvme_hdr.fw_commit_count;
		smart_power_cycles = (unsigned long)nvme_hdr.power_cycles + 1UL;
		nvme_save_persist_hdr();
	} else {
		nvme_active_fw_slot = 1;
		memcpy(nvme_fw_slots[0], "1.4.0   ", 8);
		memcpy(nvme_fw_slots[1], "        ", 8);
		memset(nvme_fw_slot_sha256, 0, sizeof(nvme_fw_slot_sha256));
		fwupd_build_bin_image("nvme", FWUPD_DEVID_NVME, FWUPD_GUID_NVME, "1.4.0",
				      FWUPD_FLAG_SIGNED_PAYLOAD | FWUPD_FLAG_DUAL_IMAGE | FWUPD_FLAG_USABLE_DURING_UPDATE,
				      nvme_admin_bounce, nvme_fw_slot_sha256[0]);
		nvme_fw_commit_count = 0;
		smart_power_cycles = 1;
		nvme_save_persist_hdr();
	}
}

static void nvme_init_queue(struct nvme_queue *q, unsigned short qid,
			    unsigned short depth)
{
	memset(q, 0, sizeof(*q));
	q->qid = qid;
	q->q_depth = depth;
	q->cq_phase = 1;
	q->cq_expected_phase = 1;
	q->next_cid = 1;
}

static void nvme_fill_id_ctrl(struct nvme_id_ctrl *ctrl)
{
	int i;

	memset(ctrl, 0, sizeof(*ctrl));
	ctrl->vid = 0x1b36;	/* Red Hat / QEMU / SIX PCIe Vendor */
	ctrl->ssvid = 0x1b36;

	memset(ctrl->sn, ' ', sizeof(ctrl->sn));
	memcpy(ctrl->sn, "SIX-NVME-2026-0001", 18);

	memset(ctrl->mn, ' ', sizeof(ctrl->mn));
	memcpy(ctrl->mn, "SIX Virtual NVMe SSD Controller", 31);

	memcpy(ctrl->fr, nvme_fw_slots[(nvme_active_fw_slot == 2) ? 1 : 0], 8);

	ctrl->rab = 6;
	ctrl->ieee[0] = 0x54;
	ctrl->ieee[1] = 0x52;
	ctrl->ieee[2] = 0x00;
	ctrl->mdts = 5;		/* 2^5 * 4KB = 128 KB max transfer */
	ctrl->cntlid = 1;
	ctrl->ver = NVME_VS(1, 4, 0);
	ctrl->oacs = 0x0006;	/* Format NVM (0x2) + FW Commit/Download (0x4) */
	ctrl->acl = 3;
	ctrl->aerl = 3;
	ctrl->frmw = 0x15;	/* Slot 1 RO, 2 FW slots, Activate Without Reset */
	ctrl->lpa = 0x02;	/* SMART / Health log per-NS supported */
	ctrl->elpe = 63;
	ctrl->npss = 0;
	ctrl->wctemp = 343;	/* 70 C warning */
	ctrl->cctemp = 358;	/* 85 C critical */
	ctrl->sqes = 0x66;	/* min 64B (2^6), max 64B (2^6) */
	ctrl->cqes = 0x44;	/* min 16B (2^4), max 16B (2^4) */
	ctrl->maxcmd = NVME_IOQ_DEPTH;
	ctrl->nn = 1;		/* 1 active namespace (/dev/nvme0n1) */
	ctrl->oncs = 0x000c;	/* Dataset Management (TRIM) + Write Zeroes */
	ctrl->vwc = 0x01;	/* Volatile Write Cache present */
	ctrl->awun = 0;
	ctrl->awupf = 0;
	ctrl->mnan = 1;
	strcpy(ctrl->subnqn, "nqn.2026-09.org.six:nvme:userdata-ssd-0001");
	for (i = 0; i < 0; i++)
		;
}

static void nvme_fill_id_ns(struct nvme_id_ns *ns)
{
	unsigned long cap_bytes = nvme_sectors * 512UL;
	unsigned long used_sectors = nvme_sectors;

	if (smart_dsm_sectors_trimmed < nvme_sectors)
		used_sectors = nvme_sectors - smart_dsm_sectors_trimmed;

	memset(ns, 0, sizeof(*ns));
	ns->nsze_lo = (unsigned int)nvme_sectors;
	ns->nsze_hi = 0;
	ns->ncap_lo = (unsigned int)nvme_sectors;
	ns->ncap_hi = 0;
	ns->nuse_lo = (unsigned int)used_sectors;
	ns->nuse_hi = 0;
	ns->nsfeat = 0x04;	/* Deallocated/unwritten logical blocks supported */
	ns->nlbaf = 0;		/* 1 LBA format (LBAF 0) */
	ns->flbas = 0;		/* Currently formatted with LBAF 0 */
	ns->dlfeat = 0x01;	/* Deallocated blocks read back as 0x00 */
	ns->nvmcap[0] = (unsigned char)(cap_bytes & 0xff);
	ns->nvmcap[1] = (unsigned char)((cap_bytes >> 8) & 0xff);
	ns->nvmcap[2] = (unsigned char)((cap_bytes >> 16) & 0xff);
	ns->nvmcap[3] = (unsigned char)((cap_bytes >> 24) & 0xff);

	/* NGUID & EUI64 for SIX Namespace 1 */
	memcpy(ns->nguid, "SIXNVME0N1USER01", 16);
	memcpy(ns->eui64, "SIXNVME1", 8);

	/* LBAF 0: 512-byte logical blocks (2^9 = 512), best relative perf */
	ns->lbaf[0].ms = 0;
	ns->lbaf[0].ds = 9;
	ns->lbaf[0].rp = 0;
}

static void nvme_fill_smart_log(struct nvme_smart_log *log)
{
	unsigned short temp_k = 308; /* 35 C (308 Kelvin) */
	unsigned long du_read = (smart_sectors_read + 999UL) / 1000UL;
	unsigned long du_written = (smart_sectors_written + 999UL) / 1000UL;

	if (smart_sectors_read > 0 && du_read == 0)
		du_read = 1;
	if (smart_sectors_written > 0 && du_written == 0)
		du_written = 1;

	memset(log, 0, sizeof(*log));
	log->critical_warning = 0x00;
	log->temperature[0] = (unsigned char)(temp_k & 0xff);
	log->temperature[1] = (unsigned char)((temp_k >> 8) & 0xff);
	log->avail_spare = 100;
	log->spare_thresh = 10;
	log->percent_used = 0;

	log->data_units_read[0] = (unsigned int)du_read;
	log->data_units_written[0] = (unsigned int)du_written;
	log->host_reads[0] = (unsigned int)smart_host_reads;
	log->host_writes[0] = (unsigned int)smart_host_writes;
	log->ctrl_busy_time[0] = (unsigned int)((smart_host_reads + smart_host_writes) / 500UL);
	log->power_cycles[0] = (unsigned int)smart_power_cycles;
	log->power_on_hours[0] = (unsigned int)(jiffies / (HZ * 3600UL));
	log->unsafe_shutdowns[0] = 0;
	log->media_errors[0] = (unsigned int)smart_media_errors;
	log->num_err_log_entries[0] = 0;
	log->temp_sensor[0] = temp_k;
	log->temp_sensor[1] = temp_k + 2;

	log->dsm_cmds = (unsigned int)smart_dsm_cmds;
	log->dsm_sectors_trimmed = (unsigned int)smart_dsm_sectors_trimmed;
	log->flush_cmds = (unsigned int)smart_flush_cmds;
	log->sectors_read_total = (unsigned int)smart_sectors_read;
	log->sectors_written_total = (unsigned int)smart_sectors_written;
}

static void nvme_fill_fw_slot_log(struct nvme_fw_slot_info_log *log)
{
	memset(log, 0, sizeof(*log));
	log->afi = (unsigned char)((nvme_active_fw_slot & 0x07) |
				   ((nvme_active_fw_slot & 0x07) << 4));
	memcpy(log->frs[0], nvme_fw_slots[0], 8);
	memcpy(log->frs[1], nvme_fw_slots[1], 8);
	memcpy(log->slot_sha256[0], nvme_fw_slot_sha256[0], 32);
	memcpy(log->slot_sha256[1], nvme_fw_slot_sha256[1], 32);
	log->fw_commit_count = nvme_fw_commit_count;
}

static void nvme_exec_admin_sqe(const struct nvme_command *cmd,
				unsigned int *out_result,
				unsigned short *out_status)
{
	*out_result = 0;
	*out_status = NVME_SC_SUCCESS;

	switch (cmd->opcode) {
	case nvme_admin_identify: {
		unsigned int cns = cmd->cdw10 & 0xff;
		void *dst = (void *)cmd->prp1_lo;
		if (!dst) {
			*out_status = NVME_SC_INVALID_FIELD;
			return;
		}
		if (cns == NVME_ID_CNS_CTRL) {
			nvme_fill_id_ctrl((struct nvme_id_ctrl *)dst);
		} else if (cns == NVME_ID_CNS_NS) {
			if (cmd->nsid != 1 && cmd->nsid != 0xffffffffU) {
				*out_status = NVME_SC_INVALID_NS;
				return;
			}
			nvme_fill_id_ns((struct nvme_id_ns *)dst);
		} else if (cns == NVME_ID_CNS_ACTIVE_NS_LIST) {
			unsigned int *list = (unsigned int *)dst;
			memset(list, 0, 4096);
			list[0] = 1;
		} else {
			*out_status = NVME_SC_INVALID_FIELD;
		}
		return;
	}

	case nvme_admin_get_log_page: {
		unsigned int lid = cmd->cdw10 & 0xff;
		void *dst = (void *)cmd->prp1_lo;
		if (!dst) {
			*out_status = NVME_SC_INVALID_FIELD;
			return;
		}
		if (lid == NVME_LOG_SMART) {
			nvme_fill_smart_log((struct nvme_smart_log *)dst);
		} else if (lid == NVME_LOG_FW_SLOT) {
			nvme_fill_fw_slot_log((struct nvme_fw_slot_info_log *)dst);
		} else if (lid == NVME_LOG_ERROR) {
			memset(dst, 0, 512);
		} else {
			*out_status = NVME_SC_INVALID_FIELD;
		}
		return;
	}

	case nvme_admin_download_fw: {
		unsigned int numd = cmd->cdw10;
		unsigned int ofst = cmd->cdw11;
		unsigned int bytes = cmd->cdw12 ? cmd->cdw12 : ((numd + 1U) * 4U);
		unsigned int offset = ofst * 4U;
		const unsigned char *src = (const unsigned char *)cmd->prp1_lo;

		if (fwupd_stream_write_chunk(&nvme_fw_stream, offset, src, bytes) < 0) {
			*out_status = NVME_SC_INVALID_FIELD;
			return;
		}
		return;
	}

	case nvme_admin_activate_fw: {
		unsigned int fs = cmd->cdw10 & 0x07;
		unsigned int ca = (cmd->cdw10 >> 3) & 0x07;
		unsigned char full_sha256[32];
		char detected_ver[12];
		int i, vlen;

		if (fs == 0)
			fs = 2; /* Default writable firmware slot is Slot 2 */
		if (fs < 1 || fs > 2) {
			*out_status = NVME_SC_FW_SLOT_INVALID;
			return;
		}

		/* CA=2: Activate existing firmware image in slot `fs` without downloading */
		if (ca == 2) {
			if (nvme_fw_slots[fs - 1][0] == ' ' || nvme_fw_slots[fs - 1][0] == '\0') {
				*out_status = NVME_SC_FW_IMAGE_ERROR;
				return;
			}
			nvme_active_fw_slot = (unsigned char)fs;
			nvme_fw_commit_count++;
			nvme_save_persist_hdr();
			printk("nvme0: Firmware Commit (CA=2): switched active slot to Slot %u (%.8s)\n",
			       fs, nvme_fw_slots[fs - 1]);
			return;
		}

		/* CA=0, 1, 3: Validate streamed .bin firmware image & SHA-256 integrity */
		if (fs == 1) {
			/* Slot 1 is factory read-only per ctrl.frmw bit 0 */
			*out_status = NVME_SC_READ_ONLY;
			return;
		}
		memset(detected_ver, 0, sizeof(detected_ver));
		if (fwupd_stream_validate_and_finalize(&nvme_fw_stream, FWUPD_DEVID_NVME,
						       8, detected_ver, full_sha256) < 0) {
			printk("nvme0: Firmware Commit REJECTED (.bin integrity / SHA-256 verification failed)\n");
			*out_status = NVME_SC_FW_IMAGE_ERROR;
			return;
		}
		if (cmd->prp1_lo &&
		    ((const struct fwupd_bin_hdr *)nvme_fw_stream.hdr_buf)->magic != FWUPD_BIN_MAGIC) {
			const char *vh = (const char *)cmd->prp1_lo;
			if (vh[0] >= 0x21 && vh[0] <= 0x7e) {
				memset(detected_ver, 0, sizeof(detected_ver));
				strncpy(detected_ver, vh, 8);
				detected_ver[8] = '\0';
			}
		}

		memset(nvme_fw_slots[fs - 1], ' ', 8);
		vlen = (int)strlen(detected_ver);
		if (vlen > 8)
			vlen = 8;
		for (i = 0; i < vlen; i++)
			nvme_fw_slots[fs - 1][i] = detected_ver[i];
		memcpy(nvme_fw_slot_sha256[fs - 1], full_sha256, 32);

		if (ca == 1 || ca == 3)
			nvme_active_fw_slot = (unsigned char)fs;
		nvme_fw_commit_count++;
		nvme_save_persist_hdr();
		printk("nvme0: Firmware Commit (CA=%u): installed .bin rev %s (%u bytes) into Slot %u (ACTIVE)\n",
		       ca, detected_ver, nvme_fw_stream.staged_len, fs);
		return;
	}

	case nvme_admin_create_cq:
	case nvme_admin_create_sq:
	case nvme_admin_delete_cq:
	case nvme_admin_delete_sq:
	case nvme_admin_abort_cmd:
		return;

	case nvme_admin_get_features:
	case nvme_admin_set_features: {
		unsigned int fid = cmd->cdw10 & 0xff;
		if (fid == 0x07)	/* Number of Queues (0-based: 1 SQ, 1 CQ) */
			*out_result = 0x00000000U;
		else if (fid == 0x06)	/* Volatile Write Cache enabled */
			*out_result = 0x00000001U;
		return;
	}

	case nvme_admin_format_nvm:
		if (cmd->nsid != 1 && cmd->nsid != 0xffffffffU) {
			*out_status = NVME_SC_INVALID_NS;
			return;
		}
		smart_dsm_sectors_trimmed = 0;
		return;

	default:
		*out_status = NVME_SC_INVALID_OPCODE;
		return;
	}
}

static void nvme_zero_sectors(unsigned long slba, unsigned long nlb)
{
	unsigned long rem = nlb;
	unsigned long cur = slba;

	if (nvme_fd < 0 || slba >= nvme_sectors)
		return;
	if (slba + rem > nvme_sectors)
		rem = nvme_sectors - slba;

	lseek(nvme_fd, (long)(nvme_base_offset + cur * 512UL), 0);
	while (rem > 0) {
		unsigned long chunk = (rem > 8) ? 8 : rem;
		write(nvme_fd, nvme_zero_page, (int)(chunk * 512UL));
		rem -= chunk;
	}
}

static void nvme_exec_io_sqe(const struct nvme_command *cmd,
			     unsigned int *out_result,
			     unsigned short *out_status)
{
	unsigned long slba = (unsigned long)cmd->cdw10;
	unsigned long nlb = (unsigned long)(cmd->cdw12 & 0xffff) + 1UL;
	unsigned long bytes = nlb * 512UL;
	unsigned char *buf = (unsigned char *)cmd->prp1_lo;

	*out_result = 0;
	*out_status = NVME_SC_SUCCESS;

	if (!nvme_online || nvme_fd < 0) {
		*out_status = NVME_SC_INVALID_NS;
		return;
	}

	switch (cmd->opcode) {
	case nvme_cmd_read:
		if (!buf) {
			*out_status = NVME_SC_INVALID_FIELD;
			return;
		}
		if (slba + nlb > nvme_sectors) {
			smart_media_errors++;
			*out_status = NVME_SC_LBA_RANGE;
			return;
		}
		lseek(nvme_fd, (long)(nvme_base_offset + slba * 512UL), 0);
		if (read(nvme_fd, buf, (int)bytes) != (int)bytes) {
			smart_media_errors++;
			*out_status = NVME_SC_LBA_RANGE;
			return;
		}
		smart_host_reads++;
		smart_sectors_read += nlb;
		return;

	case nvme_cmd_write:
		if (!buf) {
			*out_status = NVME_SC_INVALID_FIELD;
			return;
		}
		if (slba + nlb > nvme_sectors) {
			smart_media_errors++;
			*out_status = NVME_SC_LBA_RANGE;
			return;
		}
		lseek(nvme_fd, (long)(nvme_base_offset + slba * 512UL), 0);
		if (write(nvme_fd, buf, (int)bytes) != (int)bytes) {
			smart_media_errors++;
			*out_status = NVME_SC_LBA_RANGE;
			return;
		}
		smart_host_writes++;
		smart_sectors_written += nlb;
		if (smart_dsm_sectors_trimmed >= nlb)
			smart_dsm_sectors_trimmed -= nlb;
		else
			smart_dsm_sectors_trimmed = 0;
		return;

	case nvme_cmd_flush:
		smart_flush_cmds++;
		return;

	case nvme_cmd_write_zeroes:
		if (slba + nlb > nvme_sectors) {
			smart_media_errors++;
			*out_status = NVME_SC_LBA_RANGE;
			return;
		}
		nvme_zero_sectors(slba, nlb);
		smart_host_writes++;
		smart_sectors_written += nlb;
		return;

	case nvme_cmd_dsm: {
		unsigned int nr_ranges = (cmd->cdw10 & 0xff) + 1U;
		unsigned int attrs = cmd->cdw11;
		struct nvme_dsm_range *ranges = (struct nvme_dsm_range *)cmd->prp1_lo;
		unsigned int i;

		if (!ranges || nr_ranges > 256) {
			*out_status = NVME_SC_INVALID_FIELD;
			return;
		}
		smart_dsm_cmds++;
		if (attrs & NVME_DSMGMT_AD) {
			for (i = 0; i < nr_ranges; i++) {
				unsigned long r_slba = ranges[i].slba_lo;
				unsigned long r_nlb = ranges[i].nlb;
				if (r_slba + r_nlb > nvme_sectors) {
					*out_status = NVME_SC_LBA_RANGE;
					return;
				}
				if (r_nlb > 0) {
					nvme_zero_sectors(r_slba, r_nlb);
					smart_dsm_sectors_trimmed += r_nlb;
					if (smart_dsm_sectors_trimmed > nvme_sectors)
						smart_dsm_sectors_trimmed = nvme_sectors;
				}
			}
		}
		return;
	}

	default:
		*out_status = NVME_SC_INVALID_OPCODE;
		return;
	}
}

/*
 * Emulate writing the NVMe Submission Queue Tail Doorbell register
 * (BAR0 + 0x1000 + (2 * qid) * (4 << CAP.DSTRD)).
 * The controller fetches all new SQEs between sq_head and sq_tail, executes
 * them, and posts CQEs to the associated Completion Queue with Phase Tag.
 */
static void nvme_ring_sq_doorbell(struct nvme_queue *q)
{
	q->sq_doorbell_writes++;

	while (q->sq_head != q->sq_tail) {
		const struct nvme_command *sqe = &q->sq[q->sq_head];
		struct nvme_completion *cqe;
		unsigned int result = 0;
		unsigned short status = NVME_SC_SUCCESS;

		q->sq_head = (unsigned short)((q->sq_head + 1) % q->q_depth);

		if (q->qid == 0)
			nvme_exec_admin_sqe(sqe, &result, &status);
		else
			nvme_exec_io_sqe(sqe, &result, &status);

		cqe = &q->cq[q->cq_tail];
		cqe->result = result;
		cqe->rsvd = 0;
		cqe->sq_head = q->sq_head;
		cqe->sq_id = q->qid;
		cqe->command_id = sqe->command_id;
		cqe->status = (unsigned short)(((status & 0x7fff) << 1) | (q->cq_phase & 1));

		q->cq_tail++;
		if (q->cq_tail >= q->q_depth) {
			q->cq_tail = 0;
			q->cq_phase ^= 1;
		}
		q->completed_cmds++;
	}
}

/*
 * Poll the Completion Queue for the next CQE matching cq_expected_phase,
 * advance cq_head, and write the Completion Queue Head Doorbell register.
 */
static int nvme_poll_cq(struct nvme_queue *q, struct nvme_completion *out_cqe)
{
	struct nvme_completion *cqe = &q->cq[q->cq_head];

	if ((cqe->status & 1) != q->cq_expected_phase)
		return -EAGAIN;

	if (out_cqe)
		*out_cqe = *cqe;

	q->cq_head++;
	if (q->cq_head >= q->q_depth) {
		q->cq_head = 0;
		q->cq_expected_phase ^= 1;
	}
	q->cq_doorbell_writes++;
	return 0;
}

static int nvme_submit_sync_cmd(struct nvme_queue *q, struct nvme_command *cmd,
				unsigned int *out_result)
{
	struct nvme_completion cqe;
	unsigned short cid = q->next_cid++;
	unsigned short status_code;

	if (q->next_cid == 0)
		q->next_cid = 1;
	cmd->command_id = cid;

	q->sq[q->sq_tail] = *cmd;
	q->sq_tail = (unsigned short)((q->sq_tail + 1) % q->q_depth);

	nvme_ring_sq_doorbell(q);

	if (nvme_poll_cq(q, &cqe) < 0)
		return -EIO;

	if (out_result)
		*out_result = cqe.result;

	status_code = (unsigned short)((cqe.status >> 1) & 0x7fff);
	return (int)status_code;
}

/*
 * Direct sector read/write helper for Device Mapper (dm-crypt / dm-linear /
 * dm-verity) stacked on top of /dev/nvme0n1.
 */
int nvme_rw_sector(int minor, unsigned long phys_sec,
		   unsigned char *buf, int cmd_dir)
{
	struct buffer_head *bh;
	unsigned long block_nr = phys_sec >> 1;
	int sub_off = (phys_sec & 1) << 9;
	kdev_t kdev = MKDEV(NVME_MAJOR, minor);
	struct nvme_command cmd;
	int status;

	if (!nvme_online || nvme_fd < 0 || (minor != 0 && minor != 1))
		return -ENODEV;
	if (phys_sec >= nvme_sectors)
		return -EIO;

	bh = get_hash_table(kdev, block_nr, 1024);
	if (cmd_dir == READ) {
		if (bh && buffer_uptodate(bh) && buffer_dirty(bh)) {
			memcpy(buf, bh->b_data + sub_off, 512);
			brelse(bh);
			return 0;
		}
		if (bh)
			brelse(bh);
		memset(&cmd, 0, sizeof(cmd));
		cmd.opcode = nvme_cmd_read;
		cmd.nsid = 1;
		cmd.prp1_lo = (unsigned int)buf;
		cmd.cdw10 = (unsigned int)phys_sec;
		cmd.cdw12 = 0; /* 1 sector (0-based) */
		status = nvme_submit_sync_cmd(&nvme_io_q, &cmd, NULL);
		return (status == NVME_SC_SUCCESS) ? 0 : -EIO;
	} else {
		if (is_read_only(kdev) || is_read_only(MKDEV(NVME_MAJOR, 0))) {
			if (bh)
				brelse(bh);
			return -EROFS;
		}
		memset(&cmd, 0, sizeof(cmd));
		cmd.opcode = nvme_cmd_write;
		cmd.nsid = 1;
		cmd.prp1_lo = (unsigned int)buf;
		cmd.cdw10 = (unsigned int)phys_sec;
		cmd.cdw12 = 0;
		status = nvme_submit_sync_cmd(&nvme_io_q, &cmd, NULL);
		if (status != NVME_SC_SUCCESS) {
			if (bh)
				brelse(bh);
			return -EIO;
		}
		if (bh) {
			memcpy(bh->b_data + sub_off, buf, 512);
			brelse(bh);
		}
		return 0;
	}
}

void do_nvme_request(void)
{
	while (1) {
		int minor;
		unsigned long nsect;
		struct nvme_command cmd;
		int status;

		INIT_REQUEST;

		minor = MINOR(CURRENT->rq_dev);
		if (!nvme_online || nvme_fd < 0 || (minor != 0 && minor != 1)) {
			end_request(0);
			continue;
		}

		nsect = CURRENT->current_nr_sectors;
		if (CURRENT->sector + nsect > nvme_sectors) {
			end_request(0);
			continue;
		}

		if (CURRENT->cmd != READ && CURRENT->cmd != WRITE) {
			end_request(0);
			continue;
		}

		if (CURRENT->cmd == WRITE &&
		    (is_read_only(CURRENT->rq_dev) || is_read_only(MKDEV(NVME_MAJOR, 0)))) {
			end_request(0);
			continue;
		}

		memset(&cmd, 0, sizeof(cmd));
		cmd.opcode = (CURRENT->cmd == WRITE) ? nvme_cmd_write : nvme_cmd_read;
		cmd.nsid = 1;
		cmd.prp1_lo = (unsigned int)CURRENT->buffer;
		cmd.cdw10 = (unsigned int)CURRENT->sector;
		cmd.cdw12 = (unsigned int)(nsect - 1);

		status = nvme_submit_sync_cmd(&nvme_io_q, &cmd, NULL);
		if (status != NVME_SC_SUCCESS) {
			end_request(0);
			continue;
		}

		if (CURRENT->cmd == WRITE) {
			extern void dm_notify_bdev_write(kdev_t bdev);
			dm_notify_bdev_write(CURRENT->rq_dev);
		}

		CURRENT->sector += nsect;
		CURRENT->buffer += (nsect << 9);
		CURRENT->nr_sectors -= nsect;
		CURRENT->current_nr_sectors = 0;
		end_request(1);
	}
}

static int nvme_handle_passthru_ioctl(struct nvme_queue *q, unsigned long arg)
{
	struct nvme_passthru_cmd ucmd;
	struct nvme_command sqe;
	unsigned int result = 0;
	unsigned int xfer_len;
	int err, status;

	if (!arg)
		return -EINVAL;
	err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(ucmd));
	if (err)
		return err;
	memcpy_fromfs(&ucmd, (void *)arg, sizeof(ucmd));

	if (q->qid == 0 &&
	    (ucmd.opcode == nvme_admin_download_fw ||
	     ucmd.opcode == nvme_admin_activate_fw ||
	     ucmd.opcode == nvme_admin_format_nvm)) {
		if (!suser_cap(CAP_SYS_ADMIN))
			return -EACCES;
	}

	xfer_len = ucmd.data_len;
	if (xfer_len > sizeof(nvme_admin_bounce))
		xfer_len = sizeof(nvme_admin_bounce);

	memset(nvme_admin_bounce, 0, sizeof(nvme_admin_bounce));
	if (xfer_len > 0 && ucmd.addr) {
		err = verify_area(VERIFY_READ, (void *)ucmd.addr, xfer_len);
		if (err)
			return err;
		memcpy_fromfs(nvme_admin_bounce, (void *)ucmd.addr, xfer_len);
	}

	memset(&sqe, 0, sizeof(sqe));
	sqe.opcode = ucmd.opcode;
	sqe.flags = ucmd.flags;
	sqe.nsid = ucmd.nsid ? ucmd.nsid : 1;
	sqe.cdw2 = ucmd.cdw2;
	sqe.cdw3 = ucmd.cdw3;
	sqe.prp1_lo = (unsigned int)nvme_admin_bounce;
	sqe.cdw10 = ucmd.cdw10;
	sqe.cdw11 = ucmd.cdw11;
	sqe.cdw12 = ucmd.cdw12;
	sqe.cdw13 = ucmd.cdw13;
	sqe.cdw14 = ucmd.cdw14;
	sqe.cdw15 = ucmd.cdw15;

	status = nvme_submit_sync_cmd(q, &sqe, &result);
	if (status < 0)
		return status;

	if (xfer_len > 0 && ucmd.addr) {
		err = verify_area(VERIFY_WRITE, (void *)ucmd.addr, xfer_len);
		if (err)
			return err;
		memcpy_tofs((void *)ucmd.addr, nvme_admin_bounce, xfer_len);
	}

	ucmd.result = result;
	memcpy_tofs((void *)arg, &ucmd, sizeof(ucmd));
	return (status == NVME_SC_SUCCESS) ? 0 : -EIO;
}

static int nvme_handle_submit_io_ioctl(unsigned long arg)
{
	struct nvme_user_io uio;
	struct nvme_command sqe;
	unsigned int bytes;
	int err, status;

	if (!arg)
		return -EINVAL;
	err = verify_area(VERIFY_READ, (void *)arg, sizeof(uio));
	if (err)
		return err;
	memcpy_fromfs(&uio, (void *)arg, sizeof(uio));

	bytes = ((unsigned int)uio.nblocks + 1U) * 512U;
	if (bytes > sizeof(nvme_admin_bounce))
		return -EINVAL;

	memset(nvme_admin_bounce, 0, sizeof(nvme_admin_bounce));
	if (uio.opcode == nvme_cmd_write && uio.addr) {
		err = verify_area(VERIFY_READ, (void *)uio.addr, bytes);
		if (err)
			return err;
		memcpy_fromfs(nvme_admin_bounce, (void *)uio.addr, bytes);
	}

	memset(&sqe, 0, sizeof(sqe));
	sqe.opcode = uio.opcode;
	sqe.flags = uio.flags;
	sqe.nsid = 1;
	sqe.prp1_lo = (unsigned int)nvme_admin_bounce;
	sqe.cdw10 = (unsigned int)uio.slba;
	sqe.cdw12 = ((unsigned int)uio.control << 16) | (unsigned int)uio.nblocks;

	status = nvme_submit_sync_cmd(&nvme_io_q, &sqe, NULL);
	if (status != NVME_SC_SUCCESS)
		return -EIO;

	if (uio.opcode == nvme_cmd_read && uio.addr) {
		err = verify_area(VERIFY_WRITE, (void *)uio.addr, bytes);
		if (err)
			return err;
		memcpy_tofs((void *)uio.addr, nvme_admin_bounce, bytes);
	}
	return 0;
}

static int nvme_common_ioctl(struct inode *inode, struct file *file,
			     unsigned int cmd, unsigned long arg, int is_chr)
{
	int err;

	switch (cmd) {
	case NVME_IOCTL_ID:
		return is_chr ? 0 : 1;

	case NVME_IOCTL_ADMIN_CMD:
		return nvme_handle_passthru_ioctl(&nvme_admin_q, arg);

	case NVME_IOCTL_IO_CMD:
		return nvme_handle_passthru_ioctl(&nvme_io_q, arg);

	case NVME_IOCTL_SUBMIT_IO:
		return nvme_handle_submit_io_ioctl(arg);

	case NVME_IOCTL_RESET:
		if (!suser())
			return -EACCES;
		if (!is_chr)
			fsync_dev(inode->i_rdev);
		nvme_init_queue(&nvme_admin_q, 0, NVME_AQ_DEPTH);
		nvme_init_queue(&nvme_io_q, 1, NVME_IOQ_DEPTH);
		smart_power_cycles++;
		return 0;

	case BLKGETSIZE:
		if (!arg)
			return -EINVAL;
		err = verify_area(VERIFY_WRITE, (long *)arg, sizeof(long));
		if (err)
			return err;
		put_fs_long(nvme_sectors, (long *)arg);
		return 0;

	case BLKFLSBUF:
		if (!suser())
			return -EACCES;
		if (!is_chr && inode) {
			fsync_dev(inode->i_rdev);
			invalidate_buffers(inode->i_rdev);
		}
		smart_flush_cmds++;
		return 0;

	RO_IOCTLS(inode->i_rdev, arg);

	default:
		return -EINVAL;
	}
}

static int nvme_blk_ioctl(struct inode *inode, struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	if (!inode)
		return -EINVAL;
	return nvme_common_ioctl(inode, file, cmd, arg, 0);
}

static int nvme_chr_ioctl(struct inode *inode, struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	if (!inode)
		return -EINVAL;
	return nvme_common_ioctl(inode, file, cmd, arg, 1);
}

static int nvme_blk_open(struct inode *inode, struct file *filp)
{
	int minor;

	if (!inode)
		return -EINVAL;
	minor = MINOR(inode->i_rdev);
	if (minor != 0 && minor != 1)
		return -ENODEV;
	if (!nvme_online || nvme_fd < 0)
		return -ENODEV;
	return 0;
}

static void nvme_blk_release(struct inode *inode, struct file *filp)
{
	if (!inode)
		return;
	sync_dev(inode->i_rdev);
}

static int nvme_chr_open(struct inode *inode, struct file *filp)
{
	if (!inode || MINOR(inode->i_rdev) != 0)
		return -ENODEV;
	if (!nvme_online || nvme_fd < 0)
		return -ENODEV;
	return 0;
}

static void nvme_chr_release(struct inode *inode, struct file *filp)
{
}

static struct file_operations nvme_blk_fops = {
	NULL,			/* lseek */
	block_read,		/* read */
	block_write,		/* write */
	NULL,			/* readdir */
	NULL,			/* select */
	nvme_blk_ioctl,		/* ioctl */
	NULL,			/* mmap */
	nvme_blk_open,		/* open */
	nvme_blk_release,	/* release */
	block_fsync		/* fsync */
};

static struct file_operations nvme_chr_fops = {
	NULL,			/* lseek */
	NULL,			/* read */
	NULL,			/* write */
	NULL,			/* readdir */
	NULL,			/* select */
	nvme_chr_ioctl,		/* ioctl */
	NULL,			/* mmap */
	nvme_chr_open,		/* open */
	nvme_chr_release,	/* release */
	NULL			/* fsync */
};

static void nvme_geninit(struct gendisk *gd)
{
	int i;

	for (i = 0; i < NVME_MAX_MINORS; i++) {
		nvme_part[i].start_sect = 0;
		nvme_part[i].nr_sects = (i <= 1 && nvme_online) ? nvme_sectors : 0;
		nvme_sizes[i] = (i <= 1 && nvme_online) ? (int)(nvme_sectors >> (BLOCK_SIZE_BITS - 9)) : 0;
		nvme_blocksizes[i] = 1024;
	}
	blk_size[NVME_MAJOR] = nvme_sizes;
	blksize_size[NVME_MAJOR] = nvme_blocksizes;
}

static struct gendisk nvme_gendisk = {
	NVME_MAJOR,		/* major */
	"nvme",			/* major_name */
	2,			/* minor_shift */
	4,			/* max_p */
	1,			/* max_nr */
	nvme_geninit,		/* init */
	nvme_part,		/* part */
	nvme_sizes,		/* sizes */
	1,			/* nr_real */
	NULL,			/* real_devices */
	NULL			/* next */
};

int get_nvme_proc_info(char *buf)
{
	int len = 0;
	unsigned long used_sectors = nvme_sectors;
	unsigned long du_read = (smart_sectors_read + 999UL) / 1000UL;
	unsigned long du_written = (smart_sectors_written + 999UL) / 1000UL;
	const char *active_fr = nvme_fw_slots[(nvme_active_fw_slot == 2) ? 1 : 0];

	if (smart_sectors_read > 0 && du_read == 0)
		du_read = 1;
	if (smart_sectors_written > 0 && du_written == 0)
		du_written = 1;
	if (smart_dsm_sectors_trimmed < nvme_sectors)
		used_sectors = nvme_sectors - smart_dsm_sectors_trimmed;

	len += sprintf(buf + len,
		"NVMe Controller:   /dev/nvme0 (char %d:0, PCIe 0000:01:00.0)\n"
		"Model / Serial:    SIX Virtual NVMe SSD Controller / SIX-NVME-2026-0001\n"
		"Firmware / Spec:   %.8s (Active Slot %u, Slot1=%.8s [RO], Slot2=%.8s [RW], NVMe 1.4, VS=0x%08x)\n"
		"Controller Regs:   CC=0x%08x (EN=1, IOSQES=64B, IOCQES=16B) CSTS=0x%08x (RDY=1)\n"
		"Namespace 1:       /dev/nvme0n1 (block %d:0, host=%s)\n"
		"Capacity / LBA:    %lu sectors (%lu KB / %lu MB), LBAF0=512B, allocated=%lu\n"
		"\n"
		"Queue Pair  Depth  SQ_Head  SQ_Tail  CQ_Head  CQ_Tail  Phase  SQ_DB   CQ_DB   Completed\n"
		"Admin (Q0)  %-5u  %-7u  %-7u  %-7u  %-7u  %-5u  %-6lu  %-6lu  %lu\n"
		"I/O   (Q1)  %-5u  %-7u  %-7u  %-7u  %-7u  %-5u  %-6lu  %-6lu  %lu\n"
		"\n"
		"SMART / Health Information (LID 0x02):\n"
		"  Critical Warning:     0x00 (OK)\n"
		"  Composite Temp:       35 C (308 K)\n"
		"  Available Spare:      100%% (threshold 10%%)\n"
		"  Data Units Read:      %lu (%lu sectors / %lu KB)\n"
		"  Data Units Written:   %lu (%lu sectors / %lu KB)\n"
		"  Host Read Commands:   %lu\n"
		"  Host Write Commands:  %lu\n"
		"  Flush Commands:       %lu\n"
		"  DSM/TRIM Commands:    %lu (%lu sectors deallocated)\n"
		"  Power Cycles:         %lu\n"
		"  Media Errors:         %lu\n",
		NVME_CHR_MAJOR,
		active_fr, nvme_active_fw_slot, nvme_fw_slots[0],
		(nvme_fw_slots[1][0] != ' ') ? nvme_fw_slots[1] : "empty   ",
		nvme_reg_vs,
		nvme_reg_cc, nvme_reg_csts,
		NVME_MAJOR, nvme_img_path,
		nvme_sectors, nvme_sectors >> 1, nvme_sectors >> 11, used_sectors,
		nvme_admin_q.q_depth, nvme_admin_q.sq_head, nvme_admin_q.sq_tail,
		nvme_admin_q.cq_head, nvme_admin_q.cq_tail, nvme_admin_q.cq_phase,
		nvme_admin_q.sq_doorbell_writes, nvme_admin_q.cq_doorbell_writes,
		nvme_admin_q.completed_cmds,
		nvme_io_q.q_depth, nvme_io_q.sq_head, nvme_io_q.sq_tail,
		nvme_io_q.cq_head, nvme_io_q.cq_tail, nvme_io_q.cq_phase,
		nvme_io_q.sq_doorbell_writes, nvme_io_q.cq_doorbell_writes,
		nvme_io_q.completed_cmds,
		du_read, smart_sectors_read, smart_sectors_read >> 1,
		du_written, smart_sectors_written, smart_sectors_written >> 1,
		smart_host_reads,
		smart_host_writes,
		smart_flush_cmds,
		smart_dsm_cmds, smart_dsm_sectors_trimmed,
		smart_power_cycles,
		smart_media_errors);

	return len;
}

int nvme_init(void)
{
	extern char *getenv(const char *name);
	extern int ftruncate(int fd, unsigned long length);
	const char *env_path;
	long sz;

	nvme_init_queue(&nvme_admin_q, 0, NVME_AQ_DEPTH);
	nvme_init_queue(&nvme_io_q, 1, NVME_IOQ_DEPTH);

	/* Initialize Slot 1 factory firmware ("1.4.0") .bin cryptographic SHA-256 digest */
	memset(nvme_fw_slot_sha256, 0, sizeof(nvme_fw_slot_sha256));
	fwupd_build_bin_image("nvme", FWUPD_DEVID_NVME, FWUPD_GUID_NVME, "1.4.0",
			      FWUPD_FLAG_SIGNED_PAYLOAD | FWUPD_FLAG_DUAL_IMAGE | FWUPD_FLAG_USABLE_DURING_UPDATE,
			      nvme_admin_bounce, nvme_fw_slot_sha256[0]);

	env_path = getenv("NVMEDISKFILE");
	if (env_path && env_path[0]) {
		strncpy(nvme_img_path, env_path, sizeof(nvme_img_path) - 1);
		nvme_img_path[sizeof(nvme_img_path) - 1] = '\0';
	} else {
		strncpy(nvme_img_path, six_root_disk_path, sizeof(nvme_img_path) - 1);
		nvme_img_path[sizeof(nvme_img_path) - 1] = '\0';
	}

	nvme_fd = open(nvme_img_path, 2 | 0100, 0644); /* O_RDWR | O_CREAT */
	if (nvme_fd >= 0) {
		unsigned long gpt_off = SIX_PART_NVME_OFFSET;
		unsigned long gpt_bytes = SIX_PART_NVME_BYTES;
		sz = lseek(nvme_fd, 0L, 2);
		if (six_gpt_lookup_part("nvme0n1", &gpt_off, &gpt_bytes, 0) == 0 ||
		    sz >= (long)SIX_SINGLE_DISK_MIN_BYTES) {
			nvme_base_offset = gpt_off;
			nvme_sectors = gpt_bytes / 512UL;
		} else {
			nvme_base_offset = 0;
			if (sz < (long)(NVME_DEFAULT_SECTORS * 512UL)) {
				ftruncate(nvme_fd, NVME_DEFAULT_SECTORS * 512UL);
				sz = (long)(NVME_DEFAULT_SECTORS * 512UL);
			}
			nvme_sectors = (unsigned long)sz / 512UL;
		}
		nvme_online = 1;
		nvme_load_or_init_persist_hdr();
	} else {
		printk("nvme0: warning: could not open backing file %s\n", nvme_img_path);
		nvme_online = 0;
	}

	if (register_chrdev(NVME_CHR_MAJOR, "nvme", &nvme_chr_fops)) {
		printk("nvme0: unable to register char major %d\n", NVME_CHR_MAJOR);
	}
	if (register_blkdev(NVME_MAJOR, "nvme", &nvme_blk_fops)) {
		printk("nvme0: unable to register block major %d\n", NVME_MAJOR);
		return -1;
	}

	blk_dev[NVME_MAJOR].request_fn = DEVICE_REQUEST;
	read_ahead[NVME_MAJOR] = 8;
	nvme_geninit(&nvme_gendisk);
	nvme_gendisk.next = gendisk_head;
	gendisk_head = &nvme_gendisk;

	/* Submit initial Admin Identify Controller & Create I/O CQ/SQ on boot */
	if (nvme_online) {
		struct nvme_command cmd;
		memset(&cmd, 0, sizeof(cmd));
		cmd.opcode = nvme_admin_identify;
		cmd.cdw10 = NVME_ID_CNS_CTRL;
		cmd.prp1_lo = (unsigned int)nvme_admin_bounce;
		nvme_submit_sync_cmd(&nvme_admin_q, &cmd, NULL);

		memset(&cmd, 0, sizeof(cmd));
		cmd.opcode = nvme_admin_create_cq;
		cmd.cdw10 = ((NVME_IOQ_DEPTH - 1) << 16) | 1;
		cmd.cdw11 = 1;
		nvme_submit_sync_cmd(&nvme_admin_q, &cmd, NULL);

		memset(&cmd, 0, sizeof(cmd));
		cmd.opcode = nvme_admin_create_sq;
		cmd.cdw10 = ((NVME_IOQ_DEPTH - 1) << 16) | 1;
		cmd.cdw11 = (1 << 16) | 1;
		nvme_submit_sync_cmd(&nvme_admin_q, &cmd, NULL);

		printk("nvme nvme0: pci function 0000:01:00.0 (NVMe 1.4, AQ=%u, IOQ=%u)\n",
		       NVME_AQ_DEPTH, NVME_IOQ_DEPTH);
		printk("nvme0n1: detected capacity %lu sectors (%lu MB, host=%s)\n",
		       nvme_sectors, nvme_sectors >> 11, nvme_img_path);
	}

	return 0;
}
