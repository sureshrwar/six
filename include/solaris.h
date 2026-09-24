#ifndef SOLARIS_H
#define SOLARIS_H
/*
 * ram related stuff
 */
#define RAM	64	
#define SOLARIS_RAM_SIZE RAM*1024*1024
/*
 * Unused: the RAM base is computed at runtime in grow_ram() and published
 * in _ram_start.  Kept only for historical reference.
 */
#define SOLARIS_RAM_START 0x6c000
/* NB: the trailing ';' is part of this macro -- only valid in statement
 * position.  No longer used by grow_ram(); see SIX_RAM_BRK_GAP below. */
#define SIX_GAP_SIZE  10*PAGE_SIZE;

/*
 * Placement of the emulated "physical RAM" on Linux.
 *
 * SIX_RAM_BRK_GAP   how far above &_end to start looking.  Linux puts the
 *                   brk heap just past _end and randomises its base by up
 *                   to 32MB (ASLR), so we must clear that window, plus
 *                   some slack for the host heap to grow into.
 * SIX_RAM_RETRY_STEP  how far to jump when a candidate base is occupied.
 * SIX_RAM_MAX_ATTEMPTS  give up after this many tries.
 *
 * Keep the base low: mem_map[] is indexed by absolute MAP_NR(addr) and is
 * allocated out of this same region, so a high base wastes emulated RAM.
 */
#define SIX_RAM_BRK_GAP      (48*1024*1024)
#define SIX_RAM_RETRY_STEP   (16*1024*1024)
#define SIX_RAM_MAX_ATTEMPTS 16

/*
 * SIX compiles against its own include/linux/mman.h rather than the host's
 * <sys/mman.h>, so this flag isn't in scope even though the host kernel
 * supports it (Linux 4.17+).  The value is fixed by the kernel ABI.
 */
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

extern unsigned long _ram_start;
extern int RAMFD, TERMFD;

/*
 * hard disk related stuff
 *
 * SIX_MAX_DISKS must match MAX_HD in drivers/block/hd.c.  Drive 0 is the
 * root disk and is mandatory; drive 1 is optional auxiliary storage, and
 * if its file is absent NR_HD stays 1 and /dev/hdb reports ENODEV.
 *
 * HD_HEAD and HD_SECT are the emulated drive's track geometry, shared by
 * both drives.  They have to stay compile-time constants because both
 * halves of the CHS<->LBA conversion must agree on them, and one of those
 * halves is an inline function: do_hd_request() in drivers/block/hd.c
 * turns an LBA into C/H/S and writes it to the emulated task-file
 * registers, and outb_p() in <asm/io.h> turns it straight back into an
 * LBA.  The values are otherwise arbitrary -- no real drive is being
 * modelled -- so there is nothing to gain from giving the two drives
 * different track geometry, and a good deal of complexity to lose.
 *
 * The cylinder count is *not* a constant.  It is derived at runtime by
 * check_root() from the actual size of each disk file, so that the
 * capacity the driver advertises matches the number of sectors that are
 * really backed by that file.  The old fixed 1048 claimed ~516 MB from a
 * 50 MB file; since do_hard_read() ignores short reads, every access past
 * EOF quietly returned whatever the previous sector had left behind in
 * disk_buffer.  Nothing noticed while the root filesystem was the only
 * thing touching the disk, but /dev/hda makes the whole range reachable.
 *
 * HD_CYL_DEFAULT is only the fallback for a disk whose size cannot be
 * determined.
 */
#define SIX_MAX_DISKS	4
#define HD_CYL_DEFAULT	1048
#define HD_HEAD		4
#define HD_SECT		252

/*
 * Per-drive state, all indexed by the drive number that outb_p() now
 * recovers from bits 4..5 of the device/head register.
 *
 * six_disk_fd is the host file descriptor, or -1 for a drive that has no
 * backing file.  six_disk_sectors is the authoritative capacity in
 * 512-byte sectors, and is what hd_geninit() publishes as the device
 * size.  six_hd_cyl is rounded *up* from it purely so that the CHS
 * geometry can still address the final partial cylinder; it is never used
 * as a capacity.
 */
extern int  six_disk_fd[SIX_MAX_DISKS];
extern int  six_hd_cyl[SIX_MAX_DISKS];		/* derived in check_root() */
extern long six_disk_sectors[SIX_MAX_DISKS];	/* derived in check_root() */

/*
 * DISKFD is kept as the root disk's descriptor under its historical name;
 * drivers/char/keyboard.c closes it on the way out.
 */
#define DISKFD		(six_disk_fd[0])

#if (__i386__)
#define DISKFILE	"./disk/x86/root"
#define AUXDISKFILE	"./disk/x86/aux_storage-1"
#define AUXDISKFILE2	"./disk/x86/aux_storage-2"
#define BINDISKFILE	"./disk/x86/bin_storage"
#else
#define DISKFILE	"./disk/sparc/root"
#define AUXDISKFILE	"./disk/sparc/aux_storage-1"
#define AUXDISKFILE2	"./disk/sparc/aux_storage-2"
#define BINDISKFILE	"./disk/sparc/bin_storage"
#endif

struct dummy_drive_struct {
	int cyl;
	int head;
	int wpcom;
	int ctl;
	int lzone;
	int sect;
	int dummy1;
	int dummy2;
};

#define asmlinkage	/* nothing */

#define HZ 10

#define INTERVAL 1000000/HZ

#if (SIX)
#if (__i386__)
#define SIX_BIND_POINT  0x1000
#else
#define SIX_BIND_POINT  0x3000000
#endif
#endif

#if (SIX)
/*
 * same for both sparc and x86
 */
#define STACK_BASE      0x10000000
#define DEFAULT_STACK_SIZE      PAGE_SIZE*2
#endif

#define SPARC_FRAME	96

#endif //  SOLARIS_H
