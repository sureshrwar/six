
/*
 * Automatically generated C config: don't edit
 */

/*
 * Code maturity level options
 */
#undef  CONFIG_EXPERIMENTAL

/*
 * Loadable module support
 */
#undef  CONFIG_MODULES

/*
 * General setup
 */
#undef  CONFIG_MATH_EMULATION
#define CONFIG_NET 1
#define CONFIG_INET 1
#undef  CONFIG_MAX_16M
#undef  CONFIG_PCI
#undef  CONFIG_SYSVIPC
#undef  CONFIG_BINFMT_AOUT
#undef  CONFIG_BINFMT_ELF
#undef  CONFIG_KERNEL_ELF
#undef  CONFIG_M386
#undef  CONFIG_M486
#define CONFIG_M586 1
#undef  CONFIG_M686

/*
 * Floppy, IDE, and other block devices
 */
#undef  CONFIG_BLK_DEV_FD
#undef  CONFIG_BLK_DEV_IDE

/*
 * Please see Documentation/ide.txt for help/info on IDE drives
 */
#undef  CONFIG_BLK_DEV_HD_ONLY

/*
 * Additional Block Devices
 */
#undef  CONFIG_BLK_DEV_LOOP
#undef  CONFIG_BLK_DEV_MD
#undef  CONFIG_BLK_DEV_RAM
#undef  CONFIG_BLK_DEV_XD
#undef  CONFIG_BLK_DEV_HD

/*
 * SCSI support
 */
#undef  CONFIG_SCSI

/*
 * ISDN subsystem
 */
#undef  CONFIG_ISDN

/*
 * CD-ROM drivers (not for SCSI or IDE/ATAPI drives)
 */
#undef  CONFIG_CD_NO_IDESCSI

/*
 * Filesystems
 */
#undef  CONFIG_QUOTA
#undef  CONFIG_LOCK_MANDATORY
#undef  CONFIG_MINIX_FS
#undef  CONFIG_EXT_FS
#undef  CONFIG_EXT2_FS
#undef  CONFIG_XIA_FS
#undef  CONFIG_FAT_FS
#undef  CONFIG_MSDOS_FS
#undef  CONFIG_VFAT_FS
#undef  CONFIG_UMSDOS_FS
#define CONFIG_PROC_FS 1
#undef  CONFIG_NCP_FS
#undef  CONFIG_ISO9660_FS
#undef  CONFIG_HPFS_FS
#undef  CONFIG_SYSV_FS
#undef  CONFIG_UFS_FS
#define CONFIG_FUSE_FS 1

/*
 * Character devices
 */
#undef  CONFIG_SERIAL
#undef  CONFIG_DIGI
#undef  CONFIG_CYCLADES
#undef  CONFIG_STALDRV
#undef  CONFIG_RISCOM8
#undef  CONFIG_PRINTER
#undef  CONFIG_MOUSE
#undef  CONFIG_UMISC
#undef  CONFIG_QIC02_TAPE
#undef  CONFIG_FTAPE
#undef  CONFIG_APM
#undef  CONFIG_WATCHDOG
#undef  CONFIG_RTC

/*
 * Sound
 */
#undef  CONFIG_SOUND

/*
 * Kernel hacking
 */
#undef  CONFIG_PROFILE
