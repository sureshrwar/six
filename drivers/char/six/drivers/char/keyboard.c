
/*
 * linux/drivers/char/keyboard.c
 *
 * Keyboard driver for Linux v0.99 using Latin-1.
 *
 * Written for linux by Johan Myreen as a translation from
 * the assembly version by Linus (with diacriticals added)
 *
 * Some additional features added by Christoph Niemann (ChN), March 1993
 *
 * Loadable keymaps by Risto Kankkunen, May 1993
 *
 * Diacriticals redone & other small changes, aeb@cwi.nl, June 1993
 * Added decr/incr_console, dynamic keymaps, Unicode support,
 * dynamic function/string keys, led setting,  Sept 1994
 * `Sticky' modifier keys, 951006.
 *
 */

#if (SIX)
#define KEYBOARD_IRQ 22 
#else
#define KEYBOARD_IRQ 1
#endif

#define DISABLE_KBD_DURING_INTERRUPTS 0

#include <solaris.h>

#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/mm.h>
#include <linux/ptrace.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/random.h>

#include <asm/bitops.h>

#include "kbd_kern.h"
#include "diacr.h"
#include "vt_kern.h"

#define SIZE(x) (sizeof(x)/sizeof((x)[0]))

#define KBD_REPORT_ERR
#define KBD_REPORT_UNKN
/* #define KBD_IS_FOCUS_9000 */

#ifndef KBD_DEFMODE
#define KBD_DEFMODE ((1 << VC_REPEAT) | (1 << VC_META))
#endif

#ifndef KBD_DEFLEDS
/*
 * Some laptops take the 789uiojklm,. keys as number pad when NumLock
 * is on. This seems a good reason to start with NumLock off.
 */
#define KBD_DEFLEDS 0
#endif

#ifndef KBD_DEFLOCK
#define KBD_DEFLOCK 0
#endif

#include <asm/io.h>
#include <asm/system.h>

unsigned char kbd_read_mask = 0x01;     /* modified by psaux.c */

/*
 * global state includes the following, and various static variables
 * in this module: prev_scancode, shift_state, diacr, npadch, dead_key_next.
 * (last_console is now a global variable)
 */

/* shift state counters.. */
static unsigned char k_down[NR_SHIFT] = {0, };
/* keyboard key bitmap */
#define BITS_PER_LONG (8*sizeof(unsigned long))
static unsigned long key_down[256/BITS_PER_LONG] = { 0, };

static int dead_key_next = 0;


/* used only by send_data - set by keyboard_interrupt */
static volatile unsigned char reply_expected = 0;
static volatile unsigned char acknowledge = 0;
static volatile unsigned char resend = 0;


/*                                                           
 * In order to retrieve the shift_state (for the mouse server), either
 * the variable must be global, or a new procedure must be created to
 * return the value. I chose the former way.
 */                     
/*static*/ int shift_state = 0; 




struct kbd_struct kbd_table[MAX_NR_CONSOLES];
static struct tty_struct **ttytab;
static struct kbd_struct * kbd = kbd_table;
static struct tty_struct * tty = NULL;

#if DISABLE_KBD_DURING_INTERRUPTS
#define disable_keyboard()      do { send_cmd(0xAD); kb_wait(); } while (0)
#define enable_keyboard()       send_cmd(0xAE)
#else
#define disable_keyboard()      /* nothing */
#define enable_keyboard()       /* nothing */
#endif


static inline void kb_wait(void)
{
        int i;

        for (i=0; i<0x100000; i++)
                if ((inb_p(0x64) & 0x02) == 0)
                        return;
        printk(KERN_WARNING "Keyboard timed out\n");
}


static void handle_scancode(unsigned char scancode)
{
#if (SIX)
	tty = ttytab[fg_console];
	put_queue(scancode);
	if(scancode == 'q')
	{
		reset_sun_tty();
		exit(0);
	}
#else
        unsigned char keycode;
        static unsigned int prev_scancode = 0;   /* remember E0, E1 */
        char up_flag;                            /* 0 or 0200 */
        char raw_mode;

        if (reply_expected) {
          /* 0xfa, 0xfe only mean "acknowledge", "resend" for most keyboards */
          /* but they are the key-up scancodes for PF6, PF10 on a FOCUS 9000 */
                reply_expected = 0;
                if (scancode == 0xfa) {
                        acknowledge = 1;
                        return;
                } else if (scancode == 0xfe) {
                        resend = 1;
                        return;
                }
                /* strange ... */
                reply_expected = 1;
#if 0
                printk(KERN_DEBUG "keyboard reply expected - got %02x\n",
                       scancode);
#endif
        }
        if (scancode == 0) {
#ifdef KBD_REPORT_ERR
                printk(KERN_INFO "keyboard buffer overflow\n");
#endif
                prev_scancode = 0;
                return;
        }
        do_poke_blanked_console = 1;
        mark_bh(CONSOLE_BH);
        add_keyboard_randomness(scancode);

        tty = ttytab[fg_console];
        kbd = kbd_table + fg_console;
        if ((raw_mode = (kbd->kbdmode == VC_RAW))) {
                put_queue(scancode);
                /* we do not return yet, because we want to maintain
                   the key_down array, so that we have the correct
                   values when finishing RAW mode or when changing VT's */
        }

        if (scancode == 0xff) {
                /* in scancode mode 1, my ESC key generates 0xff */
                /* the calculator keys on a FOCUS 9000 generate 0xff */
#ifndef KBD_IS_FOCUS_9000
#ifdef KBD_REPORT_ERR
                if (!raw_mode)
                  printk(KERN_DEBUG "keyboard error\n");
#endif
#endif
                prev_scancode = 0;
                return;
        }

        if (scancode == 0xe0 || scancode == 0xe1) {
                prev_scancode = scancode;
                return;
        }

        /*
         *  Convert scancode to keycode, using prev_scancode.
         */
        up_flag = (scancode & 0200);
        scancode &= 0x7f;

        if (prev_scancode) {
          /*
           * usually it will be 0xe0, but a Pause key generates
           * e1 1d 45 e1 9d c5 when pressed, and nothing when released
           */
          if (prev_scancode != 0xe0) {
              if (prev_scancode == 0xe1 && scancode == 0x1d) {
                  prev_scancode = 0x100;
                  return;
              } else if (prev_scancode == 0x100 && scancode == 0x45) {
                  keycode = E1_PAUSE;
                  prev_scancode = 0;
              } else {
#ifdef KBD_REPORT_UNKN
                  if (!raw_mode)
                    printk(KERN_INFO "keyboard: unknown e1 escape sequence\n");
#endif
                  prev_scancode = 0;
                  return;
              }
          } else {
              prev_scancode = 0;
              /*
               *  The keyboard maintains its own internal caps lock and
               *  num lock statuses. In caps lock mode E0 AA precedes make
               *  code and E0 2A follows break code. In num lock mode,
               *  E0 2A precedes make code and E0 AA follows break code.
               *  We do our own book-keeping, so we will just ignore these.
               */
              /*
               *  For my keyboard there is no caps lock mode, but there are
               *  both Shift-L and Shift-R modes. The former mode generates
               *  E0 2A / E0 AA pairs, the latter E0 B6 / E0 36 pairs.
               *  So, we should also ignore the latter. - aeb@cwi.nl
              */
              if (scancode == 0x2a || scancode == 0x36)
                return;

              if (e0_keys[scancode])
                keycode = e0_keys[scancode];
              else {
#ifdef KBD_REPORT_UNKN
                  if (!raw_mode)
                    printk(KERN_INFO "keyboard: unknown scancode e0 %02x\n",
                           scancode);
#endif
                  return;
              }
          }
        } else if (scancode >= SC_LIM) {
            /* This happens with the FOCUS 9000 keyboard
               Its keys PF1..PF12 are reported to generate
               55 73 77 78 79 7a 7b 7c 74 7e 6d 6f
               Moreover, unless repeated, they do not generate
               key-down events, so we have to zero up_flag below */
            /* Also, Japanese 86/106 keyboards are reported to
               generate 0x73 and 0x7d for \ - and \ | respectively. */
            /* Also, some Brazilian keyboard is reported to produce
               0x73 and 0x7e for \ ? and KP-dot, respectively. */

          keycode = high_keys[scancode - SC_LIM];

          if (!keycode) {
              if (!raw_mode) {
#ifdef KBD_REPORT_UNKN
                  printk(KERN_INFO "keyboard: unrecognized scancode (%02x)"
                         " - ignored\n", scancode);
#endif
              }
              return;
          }
        } else
          keycode = scancode;

        /*
         * At this point the variable `keycode' contains the keycode.
         * Note: the keycode must not be 0.
         * We keep track of the up/down status of the key, and
         * return the keycode if in MEDIUMRAW mode.
         */

        if (up_flag) {
                rep = 0;
                if(!clear_bit(keycode, key_down)) {
                    /* unexpected, but this can happen:
                       maybe this was a key release for a FOCUS 9000
                       PF key; if we want to see it, we have to clear
                       up_flag */
                    if (keycode >= SC_LIM || keycode == 85)
                      up_flag = 0;
                }
        } else
                rep = set_bit(keycode, key_down);

        if (raw_mode)
                return;

        if (kbd->kbdmode == VC_MEDIUMRAW) {
                /* soon keycodes will require more than one byte */
                put_queue(keycode + up_flag);
                return;
        }

        /*
         * Small change in philosophy: earlier we defined repetition by
         *       rep = keycode == prev_keycode;
         *       prev_keycode = keycode;
         * but now by the fact that the depressed key was down already.
         * Does this ever make a difference? Yes.
         */

        /*
         *  Repeat a key only if the input buffers are empty or the
         *  characters get echoed locally. This makes key repeat usable
         *  with slow applications and under heavy loads.
         */
        if (!rep ||
            (vc_kbd_mode(kbd,VC_REPEAT) && tty &&
             (L_ECHO(tty) || (tty->driver.chars_in_buffer(tty) == 0)))) {
                u_short keysym;
                u_char type;

                /* the XOR below used to be an OR */
                int shift_final = shift_state ^ kbd->lockstate ^ kbd->slockstate;
                ushort *key_map = key_maps[shift_final];

                if (key_map != NULL) {
                        keysym = key_map[keycode];
                        type = KTYP(keysym);

                        if (type >= 0xf0) {
                            type -= 0xf0;
                            if (type == KT_LETTER) {
                                type = KT_LATIN;
                                if (vc_kbd_led(kbd, VC_CAPSLOCK)) {
                                    key_map = key_maps[shift_final ^ (1<<KG_SHIFT)];
                                    if (key_map)
                                      keysym = key_map[keycode];
                                }
                            }
                            (*key_handler[type])(keysym & 0xff, up_flag);
                            if (type != KT_SLOCK)
                              kbd->slockstate = 0;
                        } else {
                            /* maybe only if (kbd->kbdmode == VC_UNICODE) ? */
                            if (!up_flag)
                              to_utf8(keysym);
                        }
                } else {
                        /* maybe beep? */
                        /* we have at least to update shift_state */
#if 1                   /* how? two almost equivalent choices follow */
                        compute_shiftstate();
#else
                        keysym = U(plain_map[keycode]);
                        type = KTYP(keysym);
                        if (type == KT_SHIFT)
                          (*key_handler[type])(keysym & 0xff, up_flag);
#endif
                }
        }
#endif
}


static void keyboard_interrupt(int irq, void *dev_id, 
#if (SIX)
		 ucontext_t *context)
#else
		 struct pt_regs *regs)
#endif
{
        unsigned char status;
#if (!SIX)
        pt_regs = regs;
#endif
        disable_keyboard();

        status = inb_p(0x64);
        do {
                unsigned char scancode;

                /* mouse data? */
                if (status & kbd_read_mask & 0x20)
                        break;

                scancode = inb_p(0x60);
                if (status & 0x01)
                        handle_scancode(scancode);

                status = inb_p(0x64);
        } while (status & 0x01);

        mark_bh(KEYBOARD_BH);
        enable_keyboard();
}


static void put_queue(int ch)
{
        wake_up(&keypress_wait);
        if (tty) {
                tty_insert_flip_char(tty, ch, 0);
                tty_schedule_flip(tty);
        }
}


/*
 * send_data sends a character to the keyboard and waits
 * for a acknowledge, possibly retrying if asked to. Returns
 * the success status.
 */
static int send_data(unsigned char data)
{
        int retries = 3;
        int i;

        do {
                kb_wait();
                acknowledge = 0;
                resend = 0;
                reply_expected = 1;
                outb_p(data, 0x60);
#if (SIX)
                for(i=0; i<1000; i++) {
#else
                for(i=0; i<0x200000; i++) {
#endif
                        inb_p(0x64);            /* just as a delay */
                        if (acknowledge)
                                return 1;
                        if (resend)
                                break;
                }
                if (!resend)
                        return 0;
        } while (retries-- > 0);
        return 0;
}


/*
 * The leds display either (i) the status of NumLock, CapsLock, ScrollLock,
 * or (ii) whatever pattern of lights people want to show using KDSETLED,
 * or (iii) specified bits of specified words in kernel memory.
 */

static unsigned char ledstate = 0xff; /* undefined */
static unsigned char ledioctl;

unsigned char getledstate(void) {
    return ledstate;
}

void setledstate(struct kbd_struct *kbd, unsigned int led) {
    if (!(led & ~7)) {
        ledioctl = led;
        kbd->ledmode = LED_SHOW_IOCTL;
    } else
        kbd->ledmode = LED_SHOW_FLAGS;
    set_leds();
}


static struct ledptr {
    unsigned int *addr;
    unsigned int mask;
    unsigned char valid:1;
} ledptrs[3];

static inline unsigned char getleds(void) {
    struct kbd_struct *kbd = kbd_table + fg_console;
    unsigned char leds;

    if (kbd->ledmode == LED_SHOW_IOCTL)
      return ledioctl;
    leds = kbd->ledflagstate;
    if (kbd->ledmode == LED_SHOW_MEM) {
        if (ledptrs[0].valid) {
            if (*ledptrs[0].addr & ledptrs[0].mask)
              leds |= 1;
            else
              leds &= ~1;
        }
        if (ledptrs[1].valid) {
            if (*ledptrs[1].addr & ledptrs[1].mask)
              leds |= 2;
            else
              leds &= ~2;
        }
        if (ledptrs[2].valid) {
            if (*ledptrs[2].addr & ledptrs[2].mask)
              leds |= 4;
            else
              leds &= ~4;
        }
    }
    return leds;
}




/*
 * This routine is the bottom half of the keyboard interrupt
 * routine, and runs with all interrupts enabled. It does
 * console changing, led setting and copy_to_cooked, which can
 * take a reasonably long time.
 *
 * Aside from timing (which isn't really that important for
 * keyboard interrupts as they happen often), using the software
 * interrupt routines for this thing allows us to easily mask
 * this when we don't want any of the above to happen. Not yet
 * used, but this allows for easy and efficient race-condition
 * prevention later on.
 */
static void kbd_bh(void)
{
        unsigned char leds = getleds();

        if (leds != ledstate) {
                ledstate = leds;
                if (!send_data(0xed) || !send_data(leds))
                        send_data(0xf4);        /* re-enable kbd if any errors */
        }
}

#if (SIX)
static struct termios ot;
int TERMFD;

void reset_sun_tty()
{
	ioctl(TERMFD, (('T'<<8) |14) , &ot);
	close(DISKFD);
}
#endif


int kbd_init(void)
{
        int i;
        struct kbd_struct kbd0;
        extern struct tty_driver console_driver;
#if (SIX)
     	struct termios t;

    	TERMFD = open("/dev/tty", 2 );

    	ioctl(TERMFD, (('S'<<8) |011) , 0x0040 );
    	ioctl(TERMFD, (('T'<<8) |13) , &ot);

    	t = ot;
    	t.c_lflag &= ~(ECHO | ICANON | ISIG);
    	t.c_iflag &= ~(IXON | IXOFF);
    	t.c_cc[4 ] = 1;
    	t.c_cc[5 ] = 0;

    	ioctl(TERMFD, (('T'<<8) |14) , &t);
    	fcntl(TERMFD, 4 , 0x04  | 2 );
#endif
        kbd0.ledflagstate = kbd0.default_ledflagstate = KBD_DEFLEDS;
        kbd0.ledmode = LED_SHOW_FLAGS;
        kbd0.lockstate = KBD_DEFLOCK;
        kbd0.slockstate = 0;
        kbd0.modeflags = KBD_DEFMODE;
        kbd0.kbdmode = VC_XLATE;

        for (i = 0 ; i < MAX_NR_CONSOLES ; i++)
                kbd_table[i] = kbd0;

        ttytab = console_driver.table;

        request_irq(KEYBOARD_IRQ, keyboard_interrupt, 0, "keyboard", NULL);
        request_region(0x60,16,"keyboard");
#ifdef INIT_KBD
        initialize_kbd();
#endif
        init_bh(KEYBOARD_BH, kbd_bh);
        mark_bh(KEYBOARD_BH);
        return 0;
}


/* called after returning from RAW mode or when changing consoles -
   recompute k_down[] and shift_state from key_down[] */
/* maybe called when keymap is undefined, so that shiftkey release is seen */
void compute_shiftstate(void)
{               
#if (!SIX)
        int i, j, k, sym, val;
        
        shift_state = 0;
        for(i=0; i < SIZE(k_down); i++)
          k_down[i] = 0;
                   
        for(i=0; i < SIZE(key_down); i++)
          if(key_down[i]) {     /* skip this word if not a single bit on */
            k = i*BITS_PER_LONG;
            for(j=0; j<BITS_PER_LONG; j++,k++)
              if(test_bit(k, key_down)) {
                sym = U(plain_map[k]);
                if(KTYP(sym) == KT_SHIFT) {
                  val = KVAL(sym);
                  if (val == KVAL(K_CAPSSHIFT))
                    val = KVAL(K_SHIFT);
                  k_down[val]++;
                  shift_state |= (1<<val);
                }
              }   
          }     
#endif
}                 

