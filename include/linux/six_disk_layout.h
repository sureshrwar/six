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
 *   Partition 4     (151 MiB .. 207 MiB,  56 MiB): bin_storage   (/dev/hdd, super: slot_a + slot_b + sarthak)
 *   Partition 5     (207 MiB .. 223 MiB,  16 MiB): nvme0n1       (/dev/nvme0n1)
 *   Partition 6     (223 MiB .. 228 MiB,   5 MiB): ufs0          (/dev/ufsa..c, /dev/ufs-rpmb)
 *   Partition 7     (228 MiB .. 230 MiB,   2 MiB): usb_ext2      (/dev/sda: usbctl plug ext2)
 *   Partition 8     (230 MiB .. 232 MiB,   2 MiB): usb_ext4      (/dev/sda: usbctl plug ext4)
 *   Partition 9     (232 MiB .. 234 MiB,   2 MiB): usb_erofs     (/dev/sda: usbctl plug erofs)
 *   Partition 10    (234 MiB .. 236 MiB,   2 MiB): usb_ntfs      (/dev/sda: usbctl plug ntfs)
 *   Partition 11    (236 MiB .. 238 MiB,   2 MiB): usb_crypt     (/dev/sda: usbctl plug crypt)
 *   Footer          (238 MiB .. 239 MiB,   1 MiB): Backup GPT Entries & Header
 */
#ifndef _LINUX_SIX_DISK_LAYOUT_H
#define _LINUX_SIX_DISK_LAYOUT_H

#define SIX_MIB_BYTES			(1024UL * 1024UL)
#define SIX_MIB_SECTORS			(2048UL)

#define SIX_SINGLE_DISK_TOTAL_MIB	239UL
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

/*
 * Partition 4: bin_storage / Android 'super' (/dev/hdd) - 56 MiB at 151 MiB
 *
 * Internal A/B Dynamic Partition Layout on /dev/hdd (114,688 sectors):
 *   [  0 .. 16 MiB]  system_a    (sectors     0 ..  32767): 15 MiB EROFS + 1 MiB dm-verity
 *   [ 16 .. 32 MiB]  sarthak_bin (sectors 32768 ..  65535): 16 MiB go/erofs-verity
 *   [ 32 .. 36 MiB]  vendor_a    (sectors 65536 ..  73727):  3 MiB EROFS + 1 MiB dm-verity
 *   [ 36 .. 52 MiB]  system_b    (sectors 73728 .. 106495): 15 MiB EROFS + 1 MiB dm-verity
 *   [ 52 .. 56 MiB]  vendor_b    (sectors 106496.. 114687):  3 MiB EROFS + 1 MiB dm-verity
 */
#define SIX_PART_BIN_OFFSET		(151UL * SIX_MIB_BYTES)
#define SIX_PART_BIN_BYTES		(56UL * SIX_MIB_BYTES)
#define SIX_PART_BIN_SECTORS		(56UL * SIX_MIB_SECTORS)

/* Partition 5: nvme0n1 (/dev/nvme0n1) - 16 MiB at 207 MiB */
#define SIX_PART_NVME_OFFSET		(207UL * SIX_MIB_BYTES)
#define SIX_PART_NVME_BYTES		(16UL * SIX_MIB_BYTES)
#define SIX_PART_NVME_SECTORS		(16UL * SIX_MIB_SECTORS)

/* Partition 6: ufs0 (/dev/ufsa..c + RPMB) - 5 MiB at 223 MiB */
#define SIX_PART_UFS_OFFSET		(223UL * SIX_MIB_BYTES)
#define SIX_PART_UFS_BYTES		(5UL * SIX_MIB_BYTES)
#define SIX_PART_UFS_SECTORS		(5UL * SIX_MIB_SECTORS)

/* Partition 7: usb_ext2 (/dev/sda) - 2 MiB at 228 MiB */
#define SIX_PART_USB_EXT2_OFFSET	(228UL * SIX_MIB_BYTES)
#define SIX_PART_USB_EXT2_BYTES		(2UL * SIX_MIB_BYTES)
#define SIX_PART_USB_EXT2_SECTORS	(2UL * SIX_MIB_SECTORS)

/* Partition 8: usb_ext4 (/dev/sda) - 2 MiB at 230 MiB */
#define SIX_PART_USB_EXT4_OFFSET	(230UL * SIX_MIB_BYTES)
#define SIX_PART_USB_EXT4_BYTES		(2UL * SIX_MIB_BYTES)
#define SIX_PART_USB_EXT4_SECTORS	(2UL * SIX_MIB_SECTORS)

/* Partition 9: usb_erofs (/dev/sda) - 2 MiB at 232 MiB */
#define SIX_PART_USB_EROFS_OFFSET	(232UL * SIX_MIB_BYTES)
#define SIX_PART_USB_EROFS_BYTES	(2UL * SIX_MIB_BYTES)
#define SIX_PART_USB_EROFS_SECTORS	(2UL * SIX_MIB_SECTORS)

/* Partition 10: usb_ntfs (/dev/sda) - 2 MiB at 234 MiB */
#define SIX_PART_USB_NTFS_OFFSET	(234UL * SIX_MIB_BYTES)
#define SIX_PART_USB_NTFS_BYTES		(2UL * SIX_MIB_BYTES)
#define SIX_PART_USB_NTFS_SECTORS	(2UL * SIX_MIB_SECTORS)

/* Partition 11: usb_crypt (/dev/sda) - 2 MiB at 236 MiB */
#define SIX_PART_USB_CRYPT_OFFSET	(236UL * SIX_MIB_BYTES)
#define SIX_PART_USB_CRYPT_BYTES	(2UL * SIX_MIB_BYTES)
#define SIX_PART_USB_CRYPT_SECTORS	(2UL * SIX_MIB_SECTORS)

extern char six_root_disk_path[256];
extern unsigned long six_disk_offset[4];

#endif /* _LINUX_SIX_DISK_LAYOUT_H */
