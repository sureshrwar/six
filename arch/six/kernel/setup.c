
/*
 *  linux/arch/i386/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

#include <solaris.h>
#if (SIX)
#include <linux/mm.h>
#endif
#include <linux/sched.h>
#include <linux/tty.h>




char hard_math = 0;
int x86_capability = 0;       
char ignore_irq13 = 0;
char wp_works_ok = -1;          /* set if paging hardware honours WP */

/*
 * Bus types ..
 */
int EISA_bus = 0;

#if (SIX)
extern void get_kernel_mask(so_sigset_t *);
extern void sun_handler(int num, void *why, void *context);
struct drive_info_struct { int dummy[8]; } drive_info;
#else
struct drive_info_struct { char dummy[32]; } drive_info;
#endif
struct screen_info screen_info;

unsigned char aux_device_present;

extern int root_mountflags;

#if (SIX)
unsigned long _ram_start;
#endif

extern int _etext, _edata, _end;

extern char empty_zero_page[PAGE_SIZE];

#define PARAM   empty_zero_page
/*
 * Size of Extended Memory in KB
 */
#define EXT_MEM_K (*(unsigned short *) (PARAM+2))

#define DRIVE_INFO (*(struct drive_info_struct *) (PARAM+0x80))
#define SCREEN_INFO (*(struct screen_info *) (PARAM+0))
#define MOUNT_ROOT_RDONLY (*(unsigned short *) (PARAM+0x1F2))

#define ORIG_ROOT_DEV (*(unsigned short *) (PARAM+0x1FC))
#define AUX_DEVICE_INFO (*(unsigned char *) (PARAM+0x1FF))


#define COMMAND_LINE ((char *) (PARAM+2048))
#define COMMAND_LINE_SIZE 256


static char command_line[COMMAND_LINE_SIZE] = { 0, };
char saved_command_line[COMMAND_LINE_SIZE];


void setup_arch(char **cmdline_p, unsigned long * memory_start_p, unsigned long * memory_end_p)
{
        unsigned long memory_start, memory_end;
        char c = ' ', *to = command_line, *from = COMMAND_LINE;
        int len = 0;
        static unsigned char smptrap=0;

        if(smptrap==1)
        {
                return;
        }
        smptrap=1;
	ROOT_DEV = to_kdev_t(ORIG_ROOT_DEV);
        drive_info = DRIVE_INFO;
	screen_info = SCREEN_INFO;
#ifdef CONFIG_APM
        apm_bios_info = APM_BIOS_INFO;
#endif
	aux_device_present = AUX_DEVICE_INFO;
        memory_end = (1<<20) + (EXT_MEM_K<<10);
	memory_end &= PAGE_MASK;
#ifdef CONFIG_BLK_DEV_RAM
        rd_image_start = RAMDISK_FLAGS & RAMDISK_IMAGE_START_MASK;
        rd_prompt = ((RAMDISK_FLAGS & RAMDISK_PROMPT_FLAG) != 0);
        rd_doload = ((RAMDISK_FLAGS & RAMDISK_LOAD_FLAG) != 0);
#endif
#ifdef CONFIG_MAX_16M
        if (memory_end > 16*1024*1024)
                memory_end = 16*1024*1024;
#endif
        if (!MOUNT_ROOT_RDONLY)
                root_mountflags &= ~MS_RDONLY;
	else
		printk("Note : RO flag set for root filesystem.\n");
#if 0
	/*
	 * init_task is part of the kernel, so use the values accordingly.
	 * all this is purely academic - i cant really imagine these things
	 * being put to any real use anywhere down the six lane.
	 */
	init_task.mm->start_code = _stext;
	init_task.mm->end_code = __etext;
	init_task.mm->end_data =  __edata;
	init_task.mm->brk = __end;
#endif
#if (SIX)
	memory_start = _ram_start;
#else
	memory_start = (unsigned long) &_end;
#endif
	init_task.mm->start_code = TASK_SIZE;
	init_task.mm->end_code = TASK_SIZE + (unsigned long) &_etext;
	init_task.mm->end_data = TASK_SIZE + (unsigned long) &_edata;
	init_task.mm->brk = TASK_SIZE + (unsigned long) &_end;
	/* Save unparsed command line copy for /proc/cmdline */
        memcpy(saved_command_line, COMMAND_LINE, COMMAND_LINE_SIZE);
        saved_command_line[COMMAND_LINE_SIZE-1] = '\0';
/*
 * The 'mem=' argument :
 * We can limit/specify the max amount of memory available to linux using
 * this argument.In other words,this indicates the highest addressable RAM
 * address.Another purpose of this is 'mem=nopentium' which disables the
 * 4MB page tables.
 */ 
	        for (;;) {
                /*
                 * "mem=nopentium" disables the 4MB page tables.
                 * "mem=XXX[kKmM]" overrides the BIOS-reported
                 * memory size
                 */
                if (c == ' ' && *(const unsigned long *)from == *(const unsigned long *)"mem=") {
                        if (to != command_line) to--;
                        if (!memcmp(from+4, "nopentium", 9)) {
                                from += 9+4;
                                x86_capability &= ~8;
                        } else {
                                memory_end = simple_strtoul(from+4, &from, 0);
                                if ( *from == 'K' || *from == 'k' ) {
                                        memory_end = memory_end << 10;
                                        from++;
                                } else if ( *from == 'M' || *from == 'm' ) {
                                        memory_end = memory_end << 20;
                                        from++;
                                }
                        }
                }
                c = *(from++);
                if (!c)
                        break;
                if (COMMAND_LINE_SIZE <= ++len)
                        break;
                *(to++) = c;
        }
        *to = '\0';
        *cmdline_p = command_line;
        *memory_start_p = memory_start;
#if (SIX)
	*memory_end_p = memory_start + SOLARIS_RAM_SIZE;
	printk("Memory limit at 0x%x\n", *memory_end_p);
#else
        *memory_end_p = memory_end;
#endif

#ifdef CONFIG_BLK_DEV_INITRD
        if (LOADER_TYPE) {
                initrd_start = INITRD_START;
                initrd_end = INITRD_START+INITRD_SIZE;
                if (initrd_end > memory_end) {
                        printk("initrd extends beyond end of memory "
                            "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
                            initrd_end,memory_end);
                        initrd_start = 0;
                }
        }
#endif
        /* request io space for devices used on all i[345]86 PC'S */
        request_region(0x00,0x20,"dma1");
        request_region(0x40,0x20,"timer");
        request_region(0x80,0x20,"dma page reg");
        request_region(0xc0,0x20,"dma2");
        request_region(0xf0,0x10,"npu");
#if (SIX)
	install_signal_handlers();
#endif
}

#if (SIX)
void install_signal_handlers()
{
	int t;
	printk("Registering interrupt entry point...");
	for (t = 1; t <= _SIGRTMAX; t++) {
			register_interrupt_handler(t, sun_handler);
	}
	printk("Done\n");
}

void register_interrupt_handler(int t, void (* func)(int, void *, void *))
{
    struct solaris_sigaction sig;
    sig.sa_flags =  SA_SIGINFO | SA_RESTART;
    sig.sa_handler = func;
    get_kernel_mask((so_sigset_t *) &sig.sa_mask);
    sigaction(t, &sig, (struct solaris_sigaction *) 0);
}
#endif



