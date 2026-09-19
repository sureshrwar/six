#ifndef SOLARIS_H
#define SOLARIS_H
/*
 * ram related stuff
 */
#define RAM	32	
#define SOLARIS_RAM_SIZE RAM*1024*1024
#define SOLARIS_RAM_START 0x6c000
#define SIX_GAP_SIZE  10*PAGE_SIZE;

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
