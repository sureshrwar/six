/*
 * include/linux/nvme.h
 *
 * NVM Express (NVMe 1.4) specification data structures, Submission/Completion
 * Queue entries, Identify Controller/Namespace, SMART/Health Log Page, and
 * user-space ioctl ABI for SIX (/dev/nvme0, /dev/nvme0n1).
 */

#ifndef _LINUX_NVME_H
#define _LINUX_NVME_H

#define NVME_VS(maj, min, ter)	(((maj) << 16) | ((min) << 8) | (ter))

#define NVME_CHR_MAJOR_NR	59	/* /dev/nvme0 char controller */
#define NVME_BLK_MAJOR_NR	63	/* /dev/nvme0n1 block namespace */

#define NVME_DEFAULT_SECTORS	32768UL	/* 16 MB (32768 x 512B logical blocks) */
#define NVME_SECTOR_SIZE	512
#define NVME_AQ_DEPTH		32
#define NVME_IOQ_DEPTH		64

/* NVMe Controller Registers (BAR0 MMIO offsets) */
#define NVME_REG_CAP		0x0000	/* Controller Capabilities */
#define NVME_REG_VS		0x0008	/* Version */
#define NVME_REG_CC		0x0014	/* Controller Configuration */
#define NVME_REG_CSTS		0x001c	/* Controller Status */
#define NVME_REG_AQA		0x0024	/* Admin Queue Attributes */
#define NVME_REG_ASQ		0x0028	/* Admin Submission Queue Base */
#define NVME_REG_ACQ		0x0030	/* Admin Completion Queue Base */
#define NVME_REG_DBS		0x1000	/* Doorbell Registers Base */

/* Admin Command Set Opcodes */
enum nvme_admin_opcode {
	nvme_admin_delete_sq		= 0x00,
	nvme_admin_create_sq		= 0x01,
	nvme_admin_get_log_page		= 0x02,
	nvme_admin_delete_cq		= 0x04,
	nvme_admin_create_cq		= 0x05,
	nvme_admin_identify		= 0x06,
	nvme_admin_abort_cmd		= 0x08,
	nvme_admin_set_features		= 0x09,
	nvme_admin_get_features		= 0x0a,
	nvme_admin_format_nvm		= 0x80,
};

/* NVM (I/O) Command Set Opcodes */
enum nvme_nvm_opcode {
	nvme_cmd_flush			= 0x00,
	nvme_cmd_write			= 0x01,
	nvme_cmd_read			= 0x02,
	nvme_cmd_write_zeroes		= 0x08,
	nvme_cmd_dsm			= 0x09,
};

/* Identify CNS (Controller or Namespace Structure) values */
enum {
	NVME_ID_CNS_NS			= 0x00,
	NVME_ID_CNS_CTRL		= 0x01,
	NVME_ID_CNS_ACTIVE_NS_LIST	= 0x02,
};

/* Get Log Page LID values */
enum {
	NVME_LOG_ERROR			= 0x01,
	NVME_LOG_SMART			= 0x02,
	NVME_LOG_FW_SLOT		= 0x03,
};

/* Dataset Management (DSM / TRIM) attributes */
enum {
	NVME_DSMGMT_IDR			= (1 << 0),
	NVME_DSMGMT_IDW			= (1 << 1),
	NVME_DSMGMT_AD			= (1 << 2), /* Deallocate / TRIM */
};

/* Status Code values */
enum {
	NVME_SC_SUCCESS			= 0x00,
	NVME_SC_INVALID_OPCODE		= 0x01,
	NVME_SC_INVALID_FIELD		= 0x02,
	NVME_SC_INVALID_NS		= 0x0b,
	NVME_SC_LBA_RANGE		= 0x80,
	NVME_SC_READ_ONLY		= 0x82,
};

/*
 * 64-byte NVMe Submission Queue Entry (SQE)
 */
struct nvme_command {
	unsigned char	opcode;
	unsigned char	flags;
	unsigned short	command_id;
	unsigned int	nsid;
	unsigned int	cdw2;
	unsigned int	cdw3;
	unsigned int	metadata_lo;
	unsigned int	metadata_hi;
	unsigned int	prp1_lo;
	unsigned int	prp1_hi;
	unsigned int	prp2_lo;
	unsigned int	prp2_hi;
	unsigned int	cdw10;
	unsigned int	cdw11;
	unsigned int	cdw12;
	unsigned int	cdw13;
	unsigned int	cdw14;
	unsigned int	cdw15;
};

/*
 * 16-byte NVMe Completion Queue Entry (CQE)
 */
struct nvme_completion {
	unsigned int	result;		/* Command specific result (DW0) */
	unsigned int	rsvd;		/* Reserved (DW1) */
	unsigned short	sq_head;	/* SQ Head Pointer */
	unsigned short	sq_id;		/* SQ Identifier */
	unsigned short	command_id;	/* Command Identifier */
	unsigned short	status;		/* Bit 0: Phase Tag (P), Bits 15:1: Status */
};

/*
 * 16-byte Dataset Management (DSM / TRIM) Range Descriptor
 */
struct nvme_dsm_range {
	unsigned int	cattr;
	unsigned int	nlb;
	unsigned int	slba_lo;
	unsigned int	slba_hi;
};

/*
 * LBA Format Data Structure (4 bytes)
 */
struct nvme_lbaf {
	unsigned short	ms;	/* Metadata Size */
	unsigned char	ds;	/* LBA Data Size (power of 2, e.g. 9 = 512B) */
	unsigned char	rp;	/* Relative Performance */
};

/*
 * Identify Controller Data Structure (CNS 0x01, 4096 bytes)
 */
struct nvme_id_ctrl {
	unsigned short	vid;		/* PCI Vendor ID */
	unsigned short	ssvid;		/* PCI Subsystem Vendor ID */
	char		sn[20];		/* Serial Number */
	char		mn[40];		/* Model Number */
	char		fr[8];		/* Firmware Revision */
	unsigned char	rab;		/* Recommended Arbitration Burst */
	unsigned char	ieee[3];	/* IEEE OUI Identifier */
	unsigned char	cmic;		/* Controller Multi-Path I/O */
	unsigned char	mdts;		/* Maximum Data Transfer Size */
	unsigned short	cntlid;		/* Controller ID */
	unsigned int	ver;		/* Version (NVMe 1.4.0 = 0x00010400) */
	unsigned int	rtd3r;
	unsigned int	rtd3e;
	unsigned int	oaes;
	unsigned int	ctratt;
	unsigned char	rsvd100[156];
	unsigned short	oacs;		/* Optional Admin Command Support */
	unsigned char	acl;
	unsigned char	aerl;
	unsigned char	frmw;
	unsigned char	lpa;
	unsigned char	elpe;
	unsigned char	npss;
	unsigned char	avscc;
	unsigned char	apsta;
	unsigned short	wctemp;		/* Warning Composite Temp Threshold (K) */
	unsigned short	cctemp;		/* Critical Composite Temp Threshold (K) */
	unsigned char	rsvd270[242];
	unsigned char	sqes;		/* Submission Queue Entry Size (0x66) */
	unsigned char	cqes;		/* Completion Queue Entry Size (0x44) */
	unsigned short	maxcmd;		/* Maximum Outstanding Commands */
	unsigned int	nn;		/* Number of Namespaces */
	unsigned short	oncs;		/* Optional NVM Command Support */
	unsigned short	fuses;
	unsigned char	fna;
	unsigned char	vwc;		/* Volatile Write Cache */
	unsigned short	awun;
	unsigned short	awupf;
	unsigned char	nvscc;
	unsigned char	nwpc;
	unsigned short	acwu;
	unsigned char	rsvd534[2];
	unsigned int	sgls;
	unsigned int	mnan;
	unsigned char	rsvd544[224];
	char		subnqn[256];	/* NVM Subsystem NVMe Qualified Name */
	unsigned char	rsvd1024[3072];
};

/*
 * Identify Namespace Data Structure (CNS 0x00, 4096 bytes)
 */
struct nvme_id_ns {
	unsigned int	nsze_lo;	/* Namespace Size (total sectors) */
	unsigned int	nsze_hi;
	unsigned int	ncap_lo;	/* Namespace Capacity */
	unsigned int	ncap_hi;
	unsigned int	nuse_lo;	/* Namespace Utilization (allocated sectors) */
	unsigned int	nuse_hi;
	unsigned char	nsfeat;		/* Namespace Features (bit 2: Deallocated/Trim) */
	unsigned char	nlbaf;		/* Number of LBA Formats (0-based) */
	unsigned char	flbas;		/* Formatted LBA Size */
	unsigned char	mc;
	unsigned char	dpc;
	unsigned char	dps;
	unsigned char	nmic;
	unsigned char	rescap;
	unsigned char	fpi;
	unsigned char	dlfeat;		/* Deallocate Logical Block Features (0x01 = 0x00s) */
	unsigned short	nawun;
	unsigned short	nawupf;
	unsigned short	nacwu;
	unsigned short	nabsn;
	unsigned short	nabo;
	unsigned short	nabspf;
	unsigned short	noiob;
	unsigned char	nvmcap[16];	/* NVM Capacity in bytes */
	unsigned char	rsvd64[40];
	unsigned char	nguid[16];	/* Namespace Globally Unique Identifier */
	unsigned char	eui64[8];	/* IEEE Extended Unique Identifier */
	struct nvme_lbaf lbaf[16];	/* LBA Format 0..15 Support */
	unsigned char	rsvd192[3904];
};

/*
 * SMART / Health Information Log Page (LID 0x02, 512 bytes)
 */
struct nvme_smart_log {
	unsigned char	critical_warning;
	unsigned char	temperature[2];		/* Composite Temperature in Kelvin */
	unsigned char	avail_spare;		/* Available Spare % (0..100) */
	unsigned char	spare_thresh;		/* Available Spare Threshold % */
	unsigned char	percent_used;		/* Percentage Used % */
	unsigned char	rsvd6[26];
	unsigned int	data_units_read[4];	/* 1000 x 512B units read */
	unsigned int	data_units_written[4];	/* 1000 x 512B units written */
	unsigned int	host_reads[4];		/* Host Read Commands completed */
	unsigned int	host_writes[4];		/* Host Write Commands completed */
	unsigned int	ctrl_busy_time[4];	/* Controller Busy Time (minutes) */
	unsigned int	power_cycles[4];	/* Power Cycles */
	unsigned int	power_on_hours[4];	/* Power On Hours */
	unsigned int	unsafe_shutdowns[4];	/* Unsafe Shutdowns */
	unsigned int	media_errors[4];	/* Media and Data Integrity Errors */
	unsigned int	num_err_log_entries[4];	/* Error Information Log Entries */
	unsigned int	warning_temp_time;
	unsigned int	critical_comp_time;
	unsigned short	temp_sensor[8];
	/* SIX NVMe extended telemetry counters in vendor-specific bytes 216..235 */
	unsigned int	dsm_cmds;
	unsigned int	dsm_sectors_trimmed;
	unsigned int	flush_cmds;
	unsigned int	sectors_read_total;
	unsigned int	sectors_written_total;
	unsigned char	rsvd236[276];
};

/*
 * User-space NVMe Passthrough Command Structure (Admin & I/O ioctls)
 */
struct nvme_passthru_cmd {
	unsigned char	opcode;
	unsigned char	flags;
	unsigned short	rsvd1;
	unsigned int	nsid;
	unsigned int	cdw2;
	unsigned int	cdw3;
	unsigned long	metadata;
	unsigned long	addr;
	unsigned int	metadata_len;
	unsigned int	data_len;
	unsigned int	cdw10;
	unsigned int	cdw11;
	unsigned int	cdw12;
	unsigned int	cdw13;
	unsigned int	cdw14;
	unsigned int	cdw15;
	unsigned int	timeout_ms;
	unsigned int	result;
};

#define nvme_admin_cmd nvme_passthru_cmd

/*
 * User-space NVMe I/O Submission Structure (NVME_IOCTL_SUBMIT_IO)
 */
struct nvme_user_io {
	unsigned char	opcode;
	unsigned char	flags;
	unsigned short	control;
	unsigned short	nblocks;	/* 0-based sector count (0 = 1 sector) */
	unsigned short	rsvd;
	unsigned long	metadata;
	unsigned long	addr;
	unsigned long	slba;
	unsigned int	dsmgmt;
	unsigned int	reftag;
	unsigned int	apptag;
	unsigned int	appmask;
};

/* NVMe ioctl numbers */
#define NVME_IOCTL_ID		0x4E40
#define NVME_IOCTL_ADMIN_CMD	0x4E41
#define NVME_IOCTL_SUBMIT_IO	0x4E42
#define NVME_IOCTL_IO_CMD	0x4E43
#define NVME_IOCTL_RESET	0x4E44

#endif /* _LINUX_NVME_H */
