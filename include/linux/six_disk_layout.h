/*
 * include/linux/six_disk_layout.h
 *
 * Unified Single Multi-Partition Host Disk Image Layout for SIX (disk/x86/root).
 *
 * All 11 emulated storage volumes (IDE hda..hdd, NVMe nvme0n1, JEDEC UFS 4.0
 * ufsa..c + RPMB, and hotpluggable USB SCSI sda images) reside in a single
 * 215 MiB GPT-partitioned host file (./disk/x86/root):
 *
 *   LBA 0           (0 .. 511):          Protective MBR (0xEE)
 *   LBA 1           (512 .. 1023):       Primary GPT Header ("EFI PART")
 *   LBA 2..33       (1024 .. 17407):     Primary GPT Partition Entries (1..11)
 *   Partition 1     (  1 MiB ..  51 MiB,  50 MiB): root          (/dev/hda)
 *   Partition 2     ( 51 MiB .. 101 MiB,  50 MiB): aux_storage-1 (/dev/hdb)
 *   Partition 3     (101 MiB .. 151 MiB,  50 MiB): aux_storage-2 (/dev/hdc, dm)
 *   Partition 4     (151 MiB .. 183 MiB,  32 MiB): bin_storage   (/dev/hdd, verity+sarthak)
 *   Partition 5     (183 MiB .. 199 MiB,  16 MiB): nvme0n1       (/dev/nvme0n1)
 *   Partition 6     (199 MiB .. 204 MiB,   5 MiB): ufs0          (/dev/ufsa..c, /dev/ufs-rpmb)
 *   Partition 7     (204 MiB .. 206 MiB,   2 MiB): usb_ext2      (/dev/sda: usbctl plug ext2)
 *   Partition 8     (206 MiB .. 208 MiB,   2 MiB): usb_ext4      (/dev/sda: usbctl plug ext4)
 *   Partition 9     (208 MiB .. 210 MiB,   2 MiB): usb_erofs     (/dev/sda: usbctl plug erofs)
 *   Partition 10    (210 MiB .. 212 MiB,   2 MiB): usb_ntfs      (/dev/sda: usbctl plug ntfs)
 *   Partition 11    (212 MiB .. 214 MiB,   2 MiB): usb_crypt     (/dev/sda: adoptable storage)
 *   Footer          (214 MiB .. 215 MiB,   1 MiB): Backup GPT Entries & Header
 */
#ifndef _LINUX_SIX_DISK_LAYOUT_H
#define _LINUX_SIX_DISK_LAYOUT_H

#define SIX_MIB_BYTES			(1024UL * 1024UL)
#define SIX_MIB_SECTORS			(2048UL)

#define SIX_SINGLE_DISK_TOTAL_MIB	215UL
#define SIX_SINGLE_DISK_TOTAL_BYTES	(SIX_SINGLE_DISK_TOTAL_MIB * SIX_MIB_BYTES)
#define SIX_SINGLE_DISK_TOTAL_SECTORS	(SIX_SINGLE_DISK_TOTAL_MIB * SIX_MIB_SECTORS)

/* Any backing file >= 180 MiB is treated as a unified multi-partition image */
#define SIX_SINGLE_DISK_MIN_BYTES	(180UL * SIX_MIB_BYTES)

/* Partition 1: root (/dev/hda) - 50 MiB at 1 MiB */
#define SIX_PART_ROOT_OFFSET		(1UL * SIX_MIB_BYTES)
#define SIX_PART_ROOT_BYTES		(50UL * SIX_MIB_BYTES)
#define SIX_PART_ROOT_SECTORS		(50UL * SIX_MIB_SECTORS)

/* Partition 2: aux_storage-1 (/dev/hdb) - 50 MiB at 51 MiB */
#define SIX_PART_AUX1_OFFSET		(51UL * SIX_MIB_BYTES)
#define SIX_PART_AUX1_BYTES		(50UL * SIX_MIB_BYTES)
#define SIX_PART_AUX1_SECTORS		(50UL * SIX_MIB_SECTORS)

/* Partition 3: aux_storage-2 (/dev/hdc) - 50 MiB at 101 MiB */
#define SIX_PART_AUX2_OFFSET		(101UL * SIX_MIB_BYTES)
#define SIX_PART_AUX2_BYTES		(50UL * SIX_MIB_BYTES)
#define SIX_PART_AUX2_SECTORS		(50UL * SIX_MIB_SECTORS)

/* Partition 4: bin_storage (/dev/hdd) - 32 MiB at 151 MiB */
#define SIX_PART_BIN_OFFSET		(151UL * SIX_MIB_BYTES)
#define SIX_PART_BIN_BYTES		(32UL * SIX_MIB_BYTES)
#define SIX_PART_BIN_SECTORS		(32UL * SIX_MIB_SECTORS)

/* Partition 5: nvme0n1 (/dev/nvme0n1) - 16 MiB at 183 MiB */
#define SIX_PART_NVME_OFFSET		(183UL * SIX_MIB_BYTES)
#define SIX_PART_NVME_BYTES		(16UL * SIX_MIB_BYTES)
#define SIX_PART_NVME_SECTORS		(16UL * SIX_MIB_SECTORS)

/* Partition 6: ufs0 (/dev/ufsa..c + RPMB) - 5 MiB at 199 MiB */
#define SIX_PART_UFS_OFFSET		(199UL * SIX_MIB_BYTES)
#define SIX_PART_UFS_BYTES		(5UL * SIX_MIB_BYTES)
#define SIX_PART_UFS_SECTORS		(5UL * SIX_MIB_SECTORS)

/* Partition 7: usb_ext2 (/dev/sda) - 2 MiB at 204 MiB */
#define SIX_PART_USB_EXT2_OFFSET	(204UL * SIX_MIB_BYTES)
#define SIX_PART_USB_EXT2_BYTES		(2UL * SIX_MIB_BYTES)
#define SIX_PART_USB_EXT2_SECTORS	(2UL * SIX_MIB_SECTORS)

/* Partition 8: usb_ext4 (/dev/sda) - 2 MiB at 206 MiB */
#define SIX_PART_USB_EXT4_OFFSET	(206UL * SIX_MIB_BYTES)
#define SIX_PART_USB_EXT4_BYTES		(2UL * SIX_MIB_BYTES)
#define SIX_PART_USB_EXT4_SECTORS	(2UL * SIX_MIB_SECTORS)

/* Partition 9: usb_erofs (/dev/sda) - 2 MiB at 208 MiB */
#define SIX_PART_USB_EROFS_OFFSET	(208UL * SIX_MIB_BYTES)
#define SIX_PART_USB_EROFS_BYTES	(2UL * SIX_MIB_BYTES)
#define SIX_PART_USB_EROFS_SECTORS	(2UL * SIX_MIB_SECTORS)

/* Partition 10: usb_ntfs (/dev/sda) - 2 MiB at 210 MiB */
#define SIX_PART_USB_NTFS_OFFSET	(210UL * SIX_MIB_BYTES)
#define SIX_PART_USB_NTFS_BYTES		(2UL * SIX_MIB_BYTES)
#define SIX_PART_USB_NTFS_SECTORS	(2UL * SIX_MIB_SECTORS)

/* Partition 11: usb_crypt (/dev/sda) - 2 MiB at 212 MiB */
#define SIX_PART_USB_CRYPT_OFFSET	(212UL * SIX_MIB_BYTES)
#define SIX_PART_USB_CRYPT_BYTES	(2UL * SIX_MIB_BYTES)
#define SIX_PART_USB_CRYPT_SECTORS	(2UL * SIX_MIB_SECTORS)

extern char six_root_disk_path[256];
extern unsigned long six_disk_offset[4];

#endif /* _LINUX_SIX_DISK_LAYOUT_H */
