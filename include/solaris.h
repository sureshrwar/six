#ifndef SOLARIS_H
#define SOLARIS_H
/*
 * ram related stuff
 */
#define RAM	32	
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
extern int RAMFD, TERMFD, DISKFD;

/*
 * hard disk related stuff
 */
#define HD_CYL		1048
#define HD_HEAD		4
#define HD_SECT		252

#if (__i386__)
#define DISKFILE	"./disk/x86/root"
#else
#define DISKFILE	"./disk/sparc/root"
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
