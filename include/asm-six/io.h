
#ifndef _ASM_IO_H
#define _ASM_IO_H

#include <solaris.h>

/*
 * The emulated IDE controller.  These are implemented in
 * drivers/block/hd.c and are called from the inb/outb port emulation
 * further down this file.  They used to be reached via implicit
 * declaration, which assumed a return type of int -- wrong for
 * get_hard_data(), which really returns unsigned short.
 */
extern void           do_hard_read(int from, int count);
extern void           do_hard_write(int from, int count);
extern void           raise_hard_int(int cmd);
extern void           put_hard_data(unsigned short val);
extern unsigned short get_hard_data(void);

static inline void udelay(int usecs)
{               
        volatile int count =  usecs;
        volatile int i;
        
        while (count-- > 0) {
                for (i = 0; i < 20; ++i);
        }
}       


/*
 * This file contains the definitions for the x86 IO instructions
 * inb/inw/inl/outb/outw/outl and the "string versions" of the same
 * (insb/insw/insl/outsb/outsw/outsl). You can also use "pausing"
 * versions of the single-IO instructions (inb_p/inw_p/..).
 *
 * This file is not meant to be obfuscating: it's just complicated
 * to (a) handle it all in a way that makes gcc able to optimize it
 * as well as possible and (b) trying to avoid writing the same thing
 * over and over again with slight variations and possibly making a
 * mistake somewhere.
 */

/*
 * Thanks to James van Artsdalen for a better timing-fix than
 * the two short jumps: using outb's to a nonexistent port seems
 * to guarantee better timings even on fast machines.
 *
 * On the other hand, I'd like to be sure of a non-existent port:
 * I feel a bit unsafe about using 0x80 (should be safe, though)
 *
 *              Linus
 */


/*
 * readX/writeX() are used to access memory mapped devices. On some
 * architectures the memory mapped IO stuff needs to be accessed
 * differently. On the x86 architecture, we just read/write the
 * memory location directly.
 */
#define readb(addr) (*(volatile unsigned char *) (addr))
#define readw(addr) (*(volatile unsigned short *) (addr))
#define readl(addr) (*(volatile unsigned int *) (addr))

#define writeb(b,addr) ((*(volatile unsigned char *) (addr)) = (b))
#define writew(b,addr) ((*(volatile unsigned short *) (addr)) = (b))
#define writel(b,addr) ((*(volatile unsigned int *) (addr)) = (b))

#define memset_io(a,b,c)        memset((void *)(a),(b),(c))
#define memcpy_fromio(a,b,c)    memcpy((a),(void *)(b),(c))
#define memcpy_toio(a,b,c)      memcpy((void *)(a),(b),(c))

extern int hd_head[], hd_sect[];

#define inb inb_p

static inline void outb(int val, int port)
{
}


static inline void outb_p(int val, int port)
{
	static int num_sectors, start_sec, cyl, cmd, drive, head;
	int track, sector;
	switch(port)
	{
		case	1014	:	/* somethins coming */
							break;
		case	497		:	/* some wpcom crap */
							break;
		case	498		:	/* number of sectors */
							num_sectors = val;
							break;
		case	499		:	/* which sector starting */
							start_sec = val;
							break;
		case	500		:	/* now the cylinder */
							cyl = val;
							break;
		case	501		:	/* cyl >> 8 */
							break;
		case	502		:	/*	head */
							head = (val & (HD_HEAD-1));
							break;
		case	503		:	/* command!! */
							cmd = val;
							track = HD_HEAD*cyl;
							track += head;
							sector = track*HD_SECT;
							sector += (start_sec - 1);
							if(cmd == 0xc4)
								do_hard_read(sector, num_sectors);
							raise_hard_int(cmd);
							if(cmd == 0xc5)
								do_hard_write(sector, num_sectors);
							break;
		default	:
							break;		
	}
}

static inline void outw(unsigned short val, int port)
{
	switch(port)
	{
		case 0x1f0	:	/* HD DATA */
						return put_hard_data(val);
		default :
						return 1;	
	}
}

static inline  unsigned short inw(int port)
{
	switch(port)
	{
		case 0x1f0	:	/* HD DATA */
						return get_hard_data();
		default :
						return 1;	
	}
}

/*
 * Read COUNT 16-bit words from port PORT into memory starting at
 * SRC.  SRC must be at least short aligned.  This is used by the
 * IDE driver to read disk sectors.  Performance is important, but
 * the interfaces seems to be slow: just using the inlined version
 * of the inw() breaks things.
 */
static inline insw (unsigned long port, void *dst, unsigned long count)
{
        if (((unsigned long)dst) & 0x3) {
                if (((unsigned long)dst) & 0x1) {
                        panic("insw: memory not short aligned");
                }
                if (!count)
                        return;
                count--;
                *(unsigned short* ) dst = inw(port);
                dst = (unsigned short *) dst + 1;
        }

        while (count >= 2) {
#ifdef __i386__
                unsigned int w = 0;
                count -= 2;
                w |= (unsigned int)inw(port);
                w |= ((unsigned int)inw(port) << 16);
                *(unsigned int *) dst = w;
                dst = (unsigned int *) dst + 1;
#else
                unsigned int w = 0;
                unsigned int tmp;
                count -= 2;
                tmp = (unsigned int)inw(port);
                w |= (tmp << 16);
                tmp = (unsigned int)inw(port);
                w |= tmp;
                *(unsigned int *) dst = w;
                dst = (unsigned int *) dst + 1;
#endif

        }

        if (count) {
                *(unsigned short*) dst = inw(port);
        }
}


/*
 * Like insw but in the opposite direction.  This is used by the IDE
 * driver to write disk sectors.  Performance is important, but the
 * interfaces seems to be slow: just using the inlined version of the
 * outw() breaks things.
 */
static inline void outsw (unsigned long port, const void *src, unsigned long count)
{
        if (((unsigned long)src) & 0x3) {
                if (((unsigned long)src) & 0x1) {
                        panic("outsw: memory not short aligned");
                }
                outw(*(unsigned short*)src, port);
                src = (const unsigned short *) src + 1;
                --count;
        }

        while (count >= 2) {
                unsigned int w;
                count -= 2;
                w = *(unsigned int *) src;
                src = (const unsigned int *) src + 1;
#ifdef __i386__
                outw(w >>  0, port);
                outw(w >> 16, port);
#else
                outw(w >> 16, port);
                outw(w >> 0, port);
#endif

        }

        if (count) {
                outw(*(unsigned short *) src, port);
        }
}

static inline char  inb_p(int port)
{
	static char kchar[512];
	static int kcount = 0, koff = 0;
	switch(port)
	{
		case 0x1f7 : /* hard disk status :
					  * we have a superstrong disk which
					  * never cribs :-)
					  */
					 return 0x10 | 0x40 | 0x08;
		case 0x1f1 :
				/* Related to hard disk controller. We will
				 * return 1 anyway down below, and that's what
				 * we want, too.
				 */
				break;
		case 0x64 :	/* keyboard status */
				/*
				 * TERMFD is the host's /dev/tty, put into raw
				 * mode and marked O_NDELAY by
				 * six_host_tty_open_raw().  Non-blocking means
				 * read() returns -1/EAGAIN whenever nobody is
				 * typing, which is nearly always.
				 *
				 * The test here used to be "kcount == 0", so
				 * -1 was indistinguishable from a full buffer:
				 * the status port reported a keystroke waiting,
				 * and port 0x60 below then decremented kcount
				 * past zero and handed out byte after byte of
				 * the previous read -- and then whatever
				 * followed kchar[] in memory.  handle_scancode()
				 * in drivers/char/keyboard.c treats the letter
				 * 'q' as "quit SIX now", so sooner or later the
				 * emulator shot itself, with no message, while
				 * the guest was sitting innocently in read().
				 *
				 * Anything <= 0 means "no keystroke".  Clamp it
				 * so the 0x60 case cannot go negative either.
				 */
				if(kcount <= 0)
				{
					koff = 0;
					kcount = read(TERMFD, kchar, sizeof(kchar));
					if(kcount <= 0)
					{
						kcount = 0;
						return 0;
					}
					return 0x01;
				}
				else
					return 0x01;
				break;
		case 0x60 :
				if(kcount > 0)
				{
					kcount--;
					return kchar[koff++];
				}
				break;
		default :
				return 1;
	}	
	return 1;
}

extern char inb_p(int port);

#endif
