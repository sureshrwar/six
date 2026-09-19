
/*
 * selection.h
 *
 * Interface between console.c, tty_io.c, vt.c, vc_screen.c and selection.c
 */

extern int sel_cons;

extern unsigned long video_num_columns;
extern unsigned long video_num_lines;
extern unsigned long video_size_row;
extern unsigned char video_type;
extern unsigned long video_mem_base;
extern unsigned long video_mem_term;
extern unsigned long video_screen_size;
extern unsigned short video_port_reg;
extern unsigned short video_port_val;



extern int console_blanked;
extern int can_do_color;

extern unsigned char color_table[];
extern int default_red[];
extern int default_grn[];
extern int default_blu[];

extern unsigned short __real_origin;
extern unsigned short __origin;
extern unsigned char has_wrapped;

extern unsigned short *vc_scrbuf[MAX_NR_CONSOLES];


#define reverse_video_char(a)   (((a) & 0x88) | ((((a) >> 4) | ((a) << 4)) & 0x77))
#define reverse_video_short(a)  (((a) & 0x88ff) | \
        (((a) & 0x7000) >> 4) | (((a) & 0x0700) << 4))


/*
 * TGA console screen memory access
 *
 * TGA is *not* a character/attribute cell device; font bitmaps must be rendered
 * to the screen pixels.
 *
 * The "unsigned short * addr" is *ALWAYS* a kernel virtual address, either
 * of the VC's backing store, or the "shadow screen" memory where the screen
 * contents are kept, as the TGA frame buffer is *not* char/attr cells.
 *
 * We must test for an Alpha kernel virtual address that falls within
 *  the "shadow screen" memory. This condition indicates we really want
 *  to write to the screen, so, we do... :-)
 *
 * NOTE also: there's only *TWO* operations: to put/get a character/attribute.
 *  All the others needed by VGA support go away, as Not Applicable for TGA.
 */
static inline void scr_writew(unsigned short val, unsigned short * addr)
{
        /*
         * always deposit the char/attr, then see if it was to "screen" mem.
         * if so, then render the char/attr onto the real screen.
         */
        *addr = val;
        if ((unsigned long)addr < video_mem_term &&
            (unsigned long)addr >= video_mem_base) {
                tga_blitc(val, (unsigned long) addr);
        }
}


static inline unsigned short scr_readw(unsigned short * addr)
{
        return *addr;
}


static inline void memsetw(void * s, unsigned short c, unsigned int count)
{
	unsigned short * addr = (unsigned short *) s;
	int temp, row, col;
	temp = (addr - (unsigned short *)video_mem_base) >> 1;
	col = temp % video_num_columns;
	row = (temp - col)/video_num_columns;
	sun_gotoxy(row, col);
        while (count) {
                count--;
                scr_writew(c, addr++);
        }
}

static inline void memcpyw(unsigned short *to, unsigned short *from,
                           unsigned int count)
{
        while (count) {
                count--;
                scr_writew(scr_readw(from++), to++);
        }
}
