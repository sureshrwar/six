/*
 * include/linux/ufs.h
 *
 * JEDEC Universal Flash Storage (UFS 4.0 / UFSHCI 4.0) specification
 * structures, UFS Protocol Information Units (UPIUs), Descriptors,
 * Attributes, Flags, Replay Protected Memory Block (RPMB) frames, and
 * user-space BSG / RPMB ioctl ABI for SIX:
 *   - /dev/ufs-bsg0 (char 57:0)  : UFS Host Controller BSG interface
 *   - /dev/ufs-rpmb (char 57:1)  : UFS W-LUN RPMB security interface
 *   - /dev/ufsa     (block 58:0) : LUN 0 (4 MB Data LUN, ext4 /ufs)
 *   - /dev/ufsb     (block 58:1) : LUN 1 (384 KB Boot LUN A, slot_a)
 *   - /dev/ufsc     (block 58:2) : LUN 2 (384 KB Boot LUN B, slot_b)
 */

#ifndef _LINUX_UFS_H
#define _LINUX_UFS_H

#define UFS_SPEC_VERSION_4_0		0x0400
#define UFSHCI_VERSION_4_0		0x00040000U

#define UFS_CHR_MAJOR_NR		57
#define UFS_CHR_MINOR_BSG		0	/* /dev/ufs-bsg0 */
#define UFS_CHR_MINOR_RPMB		1	/* /dev/ufs-rpmb */

#define UFS_BLK_MAJOR_NR		58
#define UFS_LUN_DATA			0	/* /dev/ufsa (LUN 0) */
#define UFS_LUN_BOOT_A			1	/* /dev/ufsb (LUN 1 - Boot A) */
#define UFS_LUN_BOOT_B			2	/* /dev/ufsc (LUN 2 - Boot B) */
#define UFS_MAX_LUNS			3
#define UFS_WLUN_BOOT			0xB0
#define UFS_WLUN_UFS_DEVICE		0xD0
#define UFS_WLUN_RPMB			0xC4

/*
 * Single host backing file (./disk/x86/ufs0.img, 5 MB = 5,242,880 bytes)
 * internal flash layout:
 *   [0x000000 .. 0x00FFFF]  64 KB : Persistent UFS Controller & RPMB Header
 *   [0x010000 .. 0x02FFFF] 128 KB : W-LUN RPMB (512 x 256B blocks)
 *   [0x030000 .. 0x08FFFF] 384 KB : LUN 1 (/dev/ufsb, Boot LUN A, 768 sectors)
 *   [0x090000 .. 0x0EFFFF] 384 KB : LUN 2 (/dev/ufsc, Boot LUN B, 768 sectors)
 *   [0x0F0000 .. 0x0FFFFF]  64 KB : Reserved alignment padding
 *   [0x100000 .. 0x4FFFFF]   4 MB : LUN 0 (/dev/ufsa, Data LUN, 8192 sectors)
 */
#define UFS_IMG_HDR_OFFSET		0x000000UL
#define UFS_IMG_HDR_BYTES		0x010000UL	/* 64 KB */
#define UFS_IMG_RPMB_OFFSET		0x010000UL
#define UFS_IMG_RPMB_BYTES		0x020000UL	/* 128 KB (512 x 256B) */
#define UFS_IMG_BOOTA_OFFSET		0x030000UL
#define UFS_IMG_BOOTA_SECTORS		768UL		/* 384 KB */
#define UFS_IMG_BOOTB_OFFSET		0x090000UL
#define UFS_IMG_BOOTB_SECTORS		768UL		/* 384 KB */
#define UFS_IMG_LUN0_OFFSET		0x100000UL
#define UFS_IMG_LUN0_SECTORS		8192UL		/* 4096 KB (4 MB) */
#define UFS_IMG_TOTAL_BYTES		0x500000UL	/* 5120 KB (5 MB) */

#define UFS_UTRL_DEPTH			32
#define UFS_RPMB_TOTAL_BLOCKS		512
#define UFS_RPMB_BLOCK_SIZE		256

/* UPIU Transaction Codes */
enum ufs_upiu_transaction {
	UPIU_TRANSACTION_NOP_OUT	= 0x00,
	UPIU_TRANSACTION_COMMAND	= 0x01,
	UPIU_TRANSACTION_DATA_OUT	= 0x02,
	UPIU_TRANSACTION_TASK_REQ	= 0x04,
	UPIU_TRANSACTION_QUERY_REQ	= 0x16,
	UPIU_TRANSACTION_NOP_IN		= 0x20,
	UPIU_TRANSACTION_RESPONSE	= 0x21,
	UPIU_TRANSACTION_DATA_IN	= 0x22,
	UPIU_TRANSACTION_TASK_RSP	= 0x24,
	UPIU_TRANSACTION_READY_XFER	= 0x31,
	UPIU_TRANSACTION_QUERY_RSP	= 0x36,
};

/* SCSI CDB Opcodes carried inside COMMAND UPIU */
enum ufs_scsi_opcode {
	UFS_SCSI_TEST_UNIT_READY	= 0x00,
	UFS_SCSI_INQUIRY		= 0x12,
	UFS_SCSI_READ_CAPACITY_10	= 0x25,
	UFS_SCSI_READ_10		= 0x28,
	UFS_SCSI_WRITE_10		= 0x2A,
	UFS_SCSI_SYNC_CACHE_10		= 0x35,
	UFS_SCSI_WRITE_BUFFER		= 0x3B,
	UFS_SCSI_READ_BUFFER		= 0x3C,
	UFS_SCSI_UNMAP			= 0x42,
	UFS_SCSI_SECURITY_PROT_IN	= 0xA2,
	UFS_SCSI_SECURITY_PROT_OUT	= 0xB5,
};

/* UPIU Query Functions & Opcodes */
enum ufs_query_function {
	UPIU_QUERY_FUNC_STANDARD_READ_REQ	= 0x01,
	UPIU_QUERY_FUNC_STANDARD_WRITE_REQ	= 0x81,
};

enum ufs_query_opcode {
	UPIU_QUERY_OPCODE_NOP		= 0x00,
	UPIU_QUERY_OPCODE_READ_DESC	= 0x01,
	UPIU_QUERY_OPCODE_WRITE_DESC	= 0x02,
	UPIU_QUERY_OPCODE_READ_ATTR	= 0x03,
	UPIU_QUERY_OPCODE_WRITE_ATTR	= 0x04,
	UPIU_QUERY_OPCODE_READ_FLAG	= 0x05,
	UPIU_QUERY_OPCODE_SET_FLAG	= 0x06,
	UPIU_QUERY_OPCODE_CLEAR_FLAG	= 0x07,
	UPIU_QUERY_OPCODE_TOGGLE_FLAG	= 0x08,
};

/* UFS Descriptor IDNs */
enum ufs_desc_idn {
	UFS_DESC_IDN_DEVICE		= 0x00,
	UFS_DESC_IDN_CONFIG		= 0x01,
	UFS_DESC_IDN_UNIT		= 0x02,
	UFS_DESC_IDN_INTERCONNECT	= 0x04,
	UFS_DESC_IDN_STRING		= 0x05,
	UFS_DESC_IDN_GEOMETRY		= 0x07,
	UFS_DESC_IDN_POWER		= 0x08,
	UFS_DESC_IDN_HEALTH		= 0x09,
};

/* UFS Attribute IDNs */
enum ufs_attr_idn {
	UFS_ATTR_IDN_BOOT_LUN_EN	= 0x00,
	UFS_ATTR_IDN_POWER_MODE		= 0x02,
	UFS_ATTR_IDN_ACTIVE_ICC_LVL	= 0x03,
	UFS_ATTR_IDN_WB_FLUSH_STATUS	= 0x1C,
	UFS_ATTR_IDN_AVAIL_WB_BUFF_SIZE	= 0x1D,
	UFS_ATTR_IDN_CURR_WB_BUFF_SIZE	= 0x1E,
	UFS_ATTR_IDN_BOOT_LUN_ID	= 0x21,	/* 0x01 = Boot A (ufsb), 0x02 = Boot B (ufsc) */
};

/* UFS Flag IDNs */
enum ufs_flag_idn {
	UFS_FLAG_IDN_FDEVICEINIT	= 0x01,
	UFS_FLAG_IDN_PERMANENT_WPE	= 0x02,
	UFS_FLAG_IDN_PWR_ON_WPE		= 0x03,
	UFS_FLAG_IDN_BKOPS_EN		= 0x04,
	UFS_FLAG_IDN_PURGE_ENABLE	= 0x06,
	UFS_FLAG_IDN_WB_EN		= 0x0E,	/* WriteBooster Enable */
	UFS_FLAG_IDN_WB_BUFF_FLUSH_EN	= 0x0F,
};

/* UFS Overall Command Status (OCS) in UTRD */
enum ufs_ocs_status {
	UFS_OCS_SUCCESS			= 0x00,
	UFS_OCS_INVALID_CMD_TABLE_ATTR	= 0x01,
	UFS_OCS_INVALID_PRDT_ATTR	= 0x02,
	UFS_OCS_MISMATCH_DATA_BUF_SIZE	= 0x03,
	UFS_OCS_MISMATCH_RESP_UPIU_SIZE	= 0x04,
	UFS_OCS_PEER_COMM_FAILURE	= 0x05,
	UFS_OCS_ABORTED			= 0x06,
	UFS_OCS_FATAL_ERROR		= 0x07,
};

/*
 * 12-byte Basic UPIU Header
 */
struct ufs_upiu_header {
	unsigned char	trans_type;	/* Transaction Type */
	unsigned char	flags;		/* Flags */
	unsigned char	lun;		/* Logical Unit Number */
	unsigned char	task_tag;	/* Task Tag */
	unsigned char	cmd_set_type;	/* Command Set Type (0 = SCSI) */
	unsigned char	query_func;	/* TM / Query Function */
	unsigned char	response;	/* Response (0x00 = Target Success) */
	unsigned char	status;		/* SCSI Status or Query Status */
	unsigned char	ehs_len;	/* Total EHS Length */
	unsigned char	device_info;	/* Device Information */
	unsigned short	data_seg_len;	/* Data Segment Length */
};

/*
 * 32-byte UTP Command / Query UPIU Entry stored in the UTRL ring
 */
struct ufs_utrd_entry {
	struct ufs_upiu_header	hdr;
	/* Union of Command CDB vs Query Request/Response fields (20 bytes) */
	unsigned int		exp_data_len;	/* Expected Data Transfer Length */
	unsigned char		cdb[16];	/* 16-byte SCSI CDB or Query payload */
	unsigned long		data_addr;	/* Host/Guest buffer address */
	unsigned int		ocs;		/* Overall Command Status (OCS) */
};

/*
 * JEDEC UFS 4.0 Device Descriptor (IDN 0x00)
 */
struct ufs_device_desc {
	unsigned char	bLength;		/* 0x59 (89 bytes) */
	unsigned char	bDescriptorIDN;		/* 0x00 */
	unsigned char	bDevice;		/* 0x00 = Device */
	unsigned char	bDeviceClass;		/* 0x00 = Mass Storage */
	unsigned char	bDeviceSubClass;	/* 0x00 = Embedded Bootable */
	unsigned char	bProtocol;		/* 0x00 */
	unsigned char	bNumberLU;		/* 3 (ufsa, ufsb, ufsc) */
	unsigned char	bNumberWLU;		/* 4 (Boot, UFS Device, RPMB, Report) */
	unsigned char	bBootEnable;		/* 0x01 = Bootable */
	unsigned char	bDescrAccessEn;		/* 0x01 */
	unsigned char	bInitPowerMode;		/* 0x01 = Active Mode */
	unsigned char	bHighPriorityLUN;	/* 0x7F */
	unsigned char	bSecureRemovalType;	/* 0x00 */
	unsigned char	bSecurityLU;		/* 0x01 = RPMB supported */
	unsigned char	bBackgroundOpsTermLat;	/* 0x01 */
	unsigned char	bInitActiveICCLevel;	/* 0x0F */
	unsigned short	wSpecVersion;		/* 0x0400 (UFS 4.0) */
	unsigned short	wManufactureDate;	/* 0x0926 (Sep 2026) */
	unsigned short	wManufacturerID;	/* 0x01CE (Samsung/SIX JEDEC ID) */
	unsigned char	bUD0BaseOffset;		/* 0x1A */
	unsigned char	bUDConfigPLength;	/* 0x1A */
	unsigned char	bDeviceRTTCap;		/* 0x02 */
	unsigned short	wPeriodicRTCUpdate;
	unsigned char	bUFSFeaturesSupport;	/* 0x81 (WriteBooster + HS-G5) */
	unsigned char	bFFUTimeout;
	unsigned char	bQueueDepth;		/* 32 */
	unsigned short	wDeviceVersion;		/* 0x0400 */
	unsigned char	bNumSecureWPArea;	/* 4 */
	unsigned int	dPSAMaxDataSize;
	unsigned char	bPSADataTimeout;
	unsigned int	dExtendedUFSFeaturesSupport;
	unsigned char	bWriteBoosterBufferPreserveUserSpaceEn;
	unsigned char	bWriteBoosterBufferType;
	unsigned int	dNumSharedWriteBoosterBufferAllocUnits;
	char		manufacturer_name[16];	/* "SIX-JEDEC" */
	char		product_name[32];	/* "SIX-UFS-4.0-5M-CHIP" */
	char		serial_number[24];	/* "SIX-UFS4-2026-0001" */
	char		oem_id[16];		/* "ANDROID-SIX" */
};

/*
 * JEDEC UFS 4.0 Geometry Descriptor (IDN 0x07)
 */
struct ufs_geometry_desc {
	unsigned char	bLength;		/* 0x57 */
	unsigned char	bDescriptorIDN;		/* 0x07 */
	unsigned char	bMediaTechnology;	/* 0x00 = TLC/SLC V-NAND */
	unsigned int	qTotalRawDeviceCapacity;/* Total 512B sectors (10240) */
	unsigned char	bMaxNumberLU;		/* 8 */
	unsigned int	dSegmentSize;		/* 8192 (4 MB segment) */
	unsigned char	bAllocationUnitSize;	/* 1 */
	unsigned char	bMinAddrBlockSize;	/* 8 (4 KB min physical block) */
	unsigned char	bOptimalReadBlockSize;	/* 8 (4 KB) */
	unsigned char	bOptimalWriteBlockSize;	/* 8 (4 KB) */
	unsigned char	bMaxInBufferSize;	/* 8 */
	unsigned char	bMaxOutBufferSize;	/* 8 */
	unsigned char	bRPMB_ReadWriteSize;	/* 64 frames */
	unsigned char	bDynamicCapacityResourcePolicy;
	unsigned char	bDataOrdering;
	unsigned char	bMaxContexIDNumber;
	unsigned char	bSysDataTagUnitSize;
	unsigned char	bSysDataTagResSize;
	unsigned char	bSupportedSecRTypes;
	unsigned short	wSupportedMemoryTypes;
	unsigned int	dSystemCodeMaxNAllocU;
	unsigned short	wSystemCodeCapAdjFac;
	unsigned int	dNonPersistMaxNAllocU;
	unsigned short	wNonPersistCapAdjFac;
	unsigned int	dEnhanced1MaxNAllocU;
	unsigned short	wEnhanced1CapAdjFac;
	unsigned int	dWriteBoosterBufferMaxNAllocUnits;
	unsigned char	bDeviceMaxWriteBoosterLUs;
	unsigned char	bWriteBoosterBufferCapAdjFac;
};

/*
 * JEDEC UFS 4.0 Health Descriptor (IDN 0x09)
 */
struct ufs_health_desc {
	unsigned char	bLength;		/* 0x2D */
	unsigned char	bDescriptorIDN;		/* 0x09 */
	unsigned char	bPreEOLInfo;		/* 0x01 = Normal (<80% reserved blocks used) */
	unsigned char	bDeviceLifeTimeEstA;	/* 0x01 = 0% - 10% SLC life used */
	unsigned char	bDeviceLifeTimeEstB;	/* 0x01 = 0% - 10% TLC life used */
	unsigned int	vendor_read_cmds;
	unsigned int	vendor_write_cmds;
	unsigned int	vendor_sectors_read;
	unsigned int	vendor_sectors_written;
	unsigned int	vendor_unmap_cmds;
	unsigned int	vendor_unmap_sectors;
	unsigned int	vendor_flush_cmds;
	unsigned int	vendor_rpmb_writes;
	unsigned int	vendor_rpmb_reads;
	unsigned int	vendor_rpmb_auth_fails;
};

/*
 * JEDEC UFS 4.0 Unit Descriptor (IDN 0x02, per LUN)
 */
struct ufs_unit_desc {
	unsigned char	bLength;		/* 0x2D */
	unsigned char	bDescriptorIDN;		/* 0x02 */
	unsigned char	bUnitIndex;		/* LUN index (0, 1, 2, or 0xC4) */
	unsigned char	bLUEnable;		/* 0x01 = Enabled */
	unsigned char	bBootLunID;		/* 0x00=Data, 0x01=Boot A, 0x02=Boot B */
	unsigned char	bLUWriteProtect;	/* 0x00 = R/W */
	unsigned char	bLUQueueDepth;		/* 32 */
	unsigned char	bPSASensitive;
	unsigned char	bMemoryType;		/* 0x00=Normal, 0x01=System/Boot */
	unsigned char	bDataReliability;	/* 0x01 = Protected */
	unsigned char	bLogicalBlockSize;	/* 9 (2^9 = 512B) or 8 (256B for RPMB) */
	unsigned int	qLogicalBlockCount;	/* Number of logical blocks */
	unsigned int	dEraseBlockSize;	/* 4096 */
	unsigned char	bProvisioningType;	/* 0x02 = Thin/UNMAP supported */
	unsigned int	qPhyMemResourceCount;
	unsigned short	wContextCapabilities;
	unsigned char	bLargeUnitGranularity_M1;
	unsigned int	dLUNumWriteBoosterBufferAllocUnits;
	char		lun_name[32];
};

/*
 * User-space BSG Query ioctl payload (UFS_IOCTL_QUERY on /dev/ufs-bsg0)
 */
struct ufs_bsg_query_ioctl {
	unsigned char	opcode;		/* enum ufs_query_opcode */
	unsigned char	idn;		/* Descriptor / Attribute / Flag IDN */
	unsigned char	index;		/* LUN or descriptor index */
	unsigned char	selector;	/* Selector (0) */
	unsigned int	value;		/* Attribute value or Flag (0/1) */
	unsigned int	buf_len;	/* Descriptor buffer length */
	unsigned long	buf_addr;	/* Pointer to descriptor buffer */
	unsigned char	response;	/* Target UPIU response code (0 = OK) */
};

/*
 * User-space SCSI UPIU Command ioctl payload (UFS_IOCTL_SCSI_CMD)
 */
struct ufs_bsg_scsi_ioctl {
	unsigned char	lun;		/* Target LUN (0, 1, 2) */
	unsigned char	opcode;		/* enum ufs_scsi_opcode */
	unsigned short	rsvd;
	unsigned int	lba;		/* Starting LBA */
	unsigned int	blocks;		/* Number of 512B blocks */
	unsigned int	data_len;	/* Buffer byte length */
	unsigned long	data_addr;	/* User buffer address */
	unsigned char	status;		/* SCSI Status (0x00 = GOOD) */
};

/*
 * JEDEC RPMB 512-byte Authenticated Frame (UFS_IOCTL_RPMB on /dev/ufs-rpmb)
 */
#define RPMB_REQ_PROGRAM_KEY		0x0001
#define RPMB_REQ_GET_COUNTER		0x0002
#define RPMB_REQ_WRITE_DATA		0x0003
#define RPMB_REQ_READ_DATA		0x0004

#define RPMB_RESP_PROGRAM_KEY		0x0100
#define RPMB_RESP_GET_COUNTER		0x0200
#define RPMB_RESP_WRITE_DATA		0x0300
#define RPMB_RESP_READ_DATA		0x0400

#define RPMB_RES_OK			0x0000
#define RPMB_RES_GENERAL_FAILURE	0x0001
#define RPMB_RES_AUTH_FAILURE		0x0002
#define RPMB_RES_COUNTER_FAILURE	0x0003
#define RPMB_RES_ADDR_FAILURE		0x0004
#define RPMB_RES_WRITE_FAILURE		0x0005
#define RPMB_RES_READ_FAILURE		0x0006
#define RPMB_RES_NO_AUTH_KEY		0x0007

struct ufs_rpmb_frame {
	unsigned char	stuff[196];
	unsigned char	key_mac[32];		/* 256-bit Key or HMAC-SHA256 MAC */
	unsigned char	data[256];		/* 256-byte RPMB block payload */
	unsigned char	nonce[16];		/* 128-bit random nonce */
	unsigned int	write_counter;		/* Monotonic write counter */
	unsigned short	addr;			/* 256B block address (0..511) */
	unsigned short	block_count;		/* Number of blocks (1) */
	unsigned short	result;			/* Operation result (RPMB_RES_*) */
	unsigned short	req_resp;		/* Request / Response type */
};

/* UFS ioctl command numbers */
#define UFS_IOCTL_QUERY			0x5540
#define UFS_IOCTL_SCSI_CMD		0x5541
#define UFS_IOCTL_RPMB			0x5542
#define UFS_IOCTL_RESET			0x5543

#ifndef SG_IO
#define SG_IO				0x2285
#endif

#endif /* _LINUX_UFS_H */
