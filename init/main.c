
/*
 *  linux/init/main.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  GK 2/5/95  -  Changed to support mounting root fs via NFS
 *  Added initrd & change_root: Werner Almesberger & Hans Lermen, Feb '96
 *  Moan early if gcc is old, avoiding bogus kernels - Paul Gortmaker, May '96
 */  

#if (SIX)
#include <solaris.h>
#include <stdio.h>
#endif

#define __KERNEL_SYSCALLS__
#include <stdarg.h>

#include <asm/system.h>
#include <asm/io.h>

#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/head.h>
#include <linux/unistd.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/utsname.h>
#include <linux/ioport.h>
#include <linux/hdreg.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/blk.h>
#ifdef CONFIG_ROOT_NFS
#include <linux/nfs_fs.h>
#endif

#include <asm/bugs.h>

/*
 * Versions of gcc older than that listed below may actually compile
 * and link okay, but the end product can have subtle run time bugs.
 * To avoid associated bogus bug reports, we flatly refuse to compile
 * with a gcc that is known to be too old from the very beginning.
 */
#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 6)
#error sorry, your GCC is too old. It builds incorrect kernels.
#endif

extern char *linux_banner;
extern int console_loglevel;

static int init(void *);
extern int bdflush(void *);
extern int kswapd(void *);
extern void init_IRQ(void);
extern long console_init(long, long);
extern long kmalloc_init(long,long);
extern void sock_init(void);
extern void sysctl_init(void);

#if (SIX)
int single = 0;
char *six_banner = "SIX 1.0 Solaris UML\n" ;
static int init();
FILE *pfp;
#endif

/*
 * Boot command-line arguments
 */
#define MAX_INIT_ARGS 8
#define MAX_INIT_ENVS 8

extern void time_init(void);

static unsigned long memory_start = 0;
static unsigned long memory_end = 0;

int root_mountflags = 0;
char *execute_command = 0;

static char * argv_init[MAX_INIT_ARGS+2] = { "init", NULL, };
static char * envp_init[MAX_INIT_ENVS+2] = { "HOME=/", "TERM=linux", NULL, };

static char * argv_rc[] = { "/bin/sh", NULL };
static char * envp_rc[] = { "HOME=/", "TERM=linux", NULL };

static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", "TERM=linux", NULL };

char *get_options(char *str, int *ints)
{
        char *cur = str;
        int i=1;

        while (cur && isdigit(*cur) && i <= 10) {
                ints[i++] = simple_strtoul(cur,NULL,0);
                if ((cur = strchr(cur,',')) != NULL)
                        cur++;
        }
        ints[0] = i-1;
        return(cur);
}

static void profile_setup(char *str, int *ints)
{
        if (ints[0] > 0)
                prof_shift = (unsigned long) ints[1];
        else
#ifdef CONFIG_PROFILE_SHIFT
#if (!SIX)
                prof_shift = CONFIG_PROFILE_SHIFT;
#endif
#else
                prof_shift = 2;
#endif
}

struct {
        const char *str;
        void (*setup_func)(char *, int *);
} bootsetups[] = {
        { "reserve=", reserve_setup },
        { "profile=", profile_setup },
};

static int checksetup(char *line)
{
        int i = 0;
        int ints[11];

#ifdef CONFIG_BLK_DEV_IDE
#if (!SIX)
       /* ide driver needs the basic string, rather than pre-processed values */
        if (!strncmp(line,"ide",3) || (!strncmp(line,"hd",2) && line[2] != '='))
 	{
                ide_setup(line);
                return 1;
        }
#endif
#endif
        while (bootsetups[i].str) {
                int n = strlen(bootsetups[i].str);
                if (!strncmp(line,bootsetups[i].str,n)) {
                        bootsetups[i].setup_func(get_options(line+n,ints), ints)
;
                        return 1;
                }
                i++;
        }
        return 0;
}


/* this should be approx 2 Bo*oMips to start (note initial shift), and will
   still work even if initially too large, it will just take slightly longer */
unsigned long loops_per_sec = (1<<12);

/* This is the number of bits of precision for the loops_per_second.  Each
   bit takes on average 1.5/HZ seconds.  This (like the original) is a little
   better than 1% */
#define LPS_PREC 8

void calibrate_delay(void)
{
        int ticks;
        int loopbit;
        int lps_precision = LPS_PREC;

        loops_per_sec = (1<<12);

        printk("Calibrating delay loop.. ");
        while (loops_per_sec <<= 1) {
                /* wait for "start of" clock tick */
                ticks = jiffies;
                while (ticks == jiffies)
                        /* nothing */;
                /* Go .. */
                ticks = jiffies;
                __delay(loops_per_sec);
                ticks = jiffies - ticks;
                if (ticks)
                        break;
                }

/* Do a binary approximation to get loops_per_second set to equal one clock
   (up to lps_precision bits) */
        loops_per_sec >>= 1;
        loopbit = loops_per_sec;
        while ( lps_precision-- && (loopbit >>= 1) ) {
                loops_per_sec |= loopbit;
                ticks = jiffies;
                while (ticks == jiffies);
                ticks = jiffies;
                __delay(loops_per_sec);
                if (jiffies != ticks)   /* longer than 1 tick */
                        loops_per_sec &= ~loopbit;
        }

/* finally, adjust loops per second in terms of seconds instead of clocks */
        loops_per_sec *= HZ;
/* Round the value and print it */
        printk("ok - %lu.%02lu BogoMIPS\n",
                (loops_per_sec+2500)/500000,
                ((loops_per_sec+2500)/5000) % 100);
}


static void parse_root_dev(char * line)
{
        int base = 0;
        static struct dev_name_struct {
                const char *name;
                const int num;
        } devices[] = {
                { "nfs",     0x00ff },
                { "hda",     0x0300 },
                { "hdb",     0x0340 },
                { "hdc",     0x1600 },
                { "hdd",     0x1640 },
                { "sda",     0x0800 },
                { "sdb",     0x0810 },
                { "sdc",     0x0820 },
                { "sdd",     0x0830 },
                { "sde",     0x0840 },
                { "fd",      0x0200 },
                { "xda",     0x0d00 },
                { "xdb",     0x0d40 },
                { "ram",     0x0100 },
                { "scd",     0x0b00 },
                { "mcd",     0x1700 },
                { "cdu535",  0x1800 },
                { "aztcd",   0x1d00 },
                { "cm206cd", 0x2000 },
                { "gscd",    0x1000 },
                { "sbpcd",   0x1900 },
                { "sonycd",  0x1800 },
                { NULL, 0 }
        };

        if (strncmp(line,"/dev/",5) == 0) {
                struct dev_name_struct *dev = devices;
                line += 5;
                do {
                        int len = strlen(dev->name);
                        if (strncmp(line,dev->name,len) == 0) {
                                line += len;
                                base = dev->num;
                                break;
                        }
                        dev++;
                } while (dev->name);
        }
        ROOT_DEV = to_kdev_t(base + simple_strtoul(line,NULL,base?10:16));
}



/*
 * This is a simple kernel command line parsing function: it parses
 * the command line, and fills in the arguments/environment to init
 * as appropriate. Any cmd-line option is taken to be an environment
 * variable if it contains the character '='.
 *
 *
 * This routine also checks for options meant for the kernel.
 * These options are not given to init - they are for internal kernel use only.
 */
static void parse_options(char *line)
{
        char *next;
        int args, envs;

        if (!*line)
                return;
        args = 0;
        envs = 1;       /* TERM is set to 'linux' by default */
        next = line;
        while ((line = next) != NULL) {
                if ((next = strchr(line,' ')) != NULL)
                        *next++ = 0;
                /*
                 * Check for kernel options first..
                 */
                if (!strncmp(line,"root=",5)) {
                        parse_root_dev(line+5);
                        continue;
                }
#ifdef CONFIG_ROOT_NFS
#if (!SIX)
                if (!strncmp(line, "nfsroot=", 8)) {
                        int n;
                        line += 8;
                        ROOT_DEV = MKDEV(UNNAMED_MAJOR, 255);
                        if (line[0] == '/' || line[0] == ',' || (line[0] >= '0'
			    && line[0] <= '9')) {
                                strncpy(nfs_root_name, line, sizeof(nfs_root_name));
                                nfs_root_name[sizeof(nfs_root_name)-1] = '\0';
                                continue;
                        }
                        n = strlen(line) + strlen(NFS_ROOT);
                        if (n >= sizeof(nfs_root_name))
                                line[sizeof(nfs_root_name) - strlen(NFS_ROOT) -
				1] = '\0';
                        sprintf(nfs_root_name, NFS_ROOT, line);
                        continue;
                }
                if (!strncmp(line, "nfsaddrs=", 9)) {
                        line += 9;
                        strncpy(nfs_root_addrs, line, sizeof(nfs_root_addrs));
                        nfs_root_addrs[sizeof(nfs_root_addrs)-1] = '\0';

                       continue;
                }
#endif
#endif
                if (!strcmp(line,"ro")) {
                        root_mountflags |= MS_RDONLY;
                        continue;
                }
                if (!strcmp(line,"rw")) {
                        root_mountflags &= ~MS_RDONLY;
                        continue;
                }
                if (!strcmp(line,"debug")) {
                        console_loglevel = 10;
                        continue;
                }
                if (!strncmp(line,"init=",5)) {
                        line += 5;
                        execute_command = line;
                        continue;
                }
                if (checksetup(line))
                        continue;
                /*
                 * Then check if it's an environment variable or
                 * an option.
                 */
                if (strchr(line,'=')) {
                        if (envs >= MAX_INIT_ENVS)
                                break;
                        envp_init[++envs] = line;
                } else {
                        if (args >= MAX_INIT_ARGS)
                                break;
                        argv_init[++args] = line;
                }
        }
        argv_init[args+1] = NULL;
        envp_init[envs+1] = NULL;
}

#if (SIX)
void idle()
{
	/*	do nothing	*/
}
#endif


#ifndef __SMP__

/*
 *      Uniprocessor idle thread
 */

int cpu_idle(void *unused)
{
        for(;;)
                idle();
}
#else

/*
 *      Multiprocessor idle thread is in arch/...
 */

extern int cpu_idle(void * unused);

/*
 *      Activate a secondary processor.
 */

asmlinkage void start_secondary(void)
{
        trap_init();
        init_IRQ();
        smp_callin();
        cpu_idle(NULL);
}



/*
 *      Called by CPU#0 to activate the rest.
 */

static void smp_init(void)
{
        int i, j;
        smp_boot_cpus();

        /*
         *      Create the slave init tasks as sharing pid 0.
         *
         *      This should only happen if we have virtual CPU numbers
         *      higher than 0.
         */

        for (i=1; i<smp_num_cpus; i++)
        {
                struct task_struct *n, *p;

                j = cpu_logical_map[i];
                /*
                 *      We use kernel_thread for the idlers which are
                 *      unlocked tasks running in kernel space.
                 */
                kernel_thread(cpu_idle, NULL, CLONE_PID);
                /*
                 *      Don't assume linear processor numbering
                 */
                current_set[j]=task[i];
                current_set[j]->processor=j;
                cli();
                n = task[i]->next_run;
                p = task[i]->prev_run;
                nr_running--;
                n->prev_run = p;
                p->next_run = n;
                task[i]->next_run = task[i]->prev_run = task[i];
                sti();
        }
}

/*
 *      The autoprobe routines assume CPU#0 on the i386
 *      so we don't actually set the game in motion until
 *      they are finished.
 */

static void smp_begin(void)
{
        smp_threads_ready=1;
        smp_commence();
}

#endif



#if (SIX)
extern char empty_zero_page[PAGE_SIZE];

so_sigset_t uni_lock;
so_sigset_t uni_unlock;
so_sigset_t saved_sig;

setup_masks()
{
/*
 * Get the lock and unlock masks into a global place
 * so we don't need a function call each time.
 */
	get_kernel_mask(&uni_lock);
   	sosigemptyset(&uni_unlock);
}

setup_files()
{
	pfp = fopen("/tmp/.pid", "w");
	if(pfp)
	{
		fprintf(pfp, "%d\n", getpid());
		fclose(pfp);
	}
	else 
		printk("warning: could not open pid file\n");
}

add_root()
{
	strcpy(empty_zero_page+2048, "root=/dev/hda0");
}

void wait_for_key()
{
	printf("Hit ENTER to continue :");
	fflush(stdout);
	getchar();
}

void setup_disk_info()
{
	struct dummy_drive_struct tmp = { HD_CYL, HD_HEAD, 0, 0, 0, HD_SECT, 0, 0 };
	memcpy(empty_zero_page+0x80, &tmp, sizeof(tmp));
#if 0
	*(char *)(empty_zero_page+0x1F2) = MS_RDONLY;
#endif
}

void check_root()
{
	char *root;
	extern int DISKFD;
	/*
	 * Check env first...
	 */
        if (!(root=getenv("DISKFILE")))
		root = DISKFILE; /* else look in current dir */

        DISKFD = open(root, O_RDWR);

        if(DISKFD < 0)
	{
		fprintf(stderr, "ERROR : cant locate root disk\n\n" 
				"the root disk file should be accessible either:\n\n"
				"* as disk/sparc/root (or disk/x86/root) under the current\n"
				"  directory\n"
				"* through the environment variable DISKFILE\n");
		exit(1);
	}
}

void grow_ram()
{
	extern int RAMFD, _end;
	void *ret;
	unsigned long base;
	int attempt;

	RAMFD = fileno(tmpfile());
	ftruncate(RAMFD, SOLARIS_RAM_SIZE);

	/*
	 * Where to put the emulated "physical memory".
	 *
	 * The 2005 code mapped it MAP_FIXED at &_end + 2*SIX_GAP_SIZE, I.e.
	 * about 80KB past the end of bss.  That was fine on Solaris, but on
	 * Linux the brk heap starts just above _end -- and with ASLR its
	 * base is randomised by up to 32MB.  MAP_FIXED does not fail on a
	 * collision, it silently replaces whatever is already mapped, so
	 * this quietly unmapped glibc's heap and the next malloc() aborted
	 * with an assertion failure inside sysmalloc().
	 *
	 * So: start looking beyond the ASLR brk window, and use
	 * MAP_FIXED_NOREPLACE (Linux 4.17+) so that a collision is reported
	 * as an error rather than papered over.  If the chosen base is
	 * occupied we walk upwards and try again.
	 *
	 * The base is deliberately kept as LOW as possible.  mem_map[] is
	 * indexed by absolute MAP_NR(addr) = addr >> PAGE_SHIFT and is
	 * carved out of this very region, so every extra megabyte of base
	 * address costs real emulated RAM.
	 */
	base = PAGE_ALIGN((unsigned long)&_end + SIX_RAM_BRK_GAP);

	for (attempt = 0; attempt < SIX_RAM_MAX_ATTEMPTS; attempt++) {
		printk("Mapping %dMB of memory at 0x%08lx ...",
		       RAM, base);

		ret = mmap((void *)base, SOLARIS_RAM_SIZE,
			   PROT_READ | PROT_WRITE | PROT_EXEC,
			   MAP_SHARED | MAP_FIXED_NOREPLACE, RAMFD, 0);

		if (ret != MAP_FAILED && (unsigned long)ret == base) {
			printk(" ok\n");
			_ram_start = base;
			return;
		}

		/*
		 * MAP_FIXED_NOREPLACE gives EEXIST when the range is taken.
		 * Anything else is a real failure worth reporting.
		 */
		if (ret != MAP_FAILED)
			munmap(ret, SOLARIS_RAM_SIZE);
		printk(" in use, retrying higher\n");
		base += SIX_RAM_RETRY_STEP;
	}

	printk("\nKernel abort : could not place %dMB of emulated RAM\n", RAM);
	perror("Diagnostics: ");
	exit(1);
}

void main(int argc, char *argv[])
{
	if (argc == 2 && !strcmp(argv[1], "single"))
        	single = 1;
	/* We are opening some files for recording debug info, the pid etc */
	setup_files();
	/* Now wait for a keypress */
	wait_for_key();
	/* ensure that we have a root disk */
	check_root();
	/* build the signal masks for locked and unlocked conditions and store them */
	setup_masks();
	/* update empty_zero_page with information abt the hard disk setup. We are
	 * simulating a bios. 
	 */
	setup_disk_info();
	/*
	 * Bios simulation again - update empty_zero_page with information about
	 * the root disk.
	 */
	add_root();
        /* Bios simulation etc done, now block all sun signals */
        cli();
	/*
	 * printk actually doesnt print this to the console yet. It just
	 * buffers the string till a console device registers itself with
	 * the kernel. Then the kernel passes the buffered console log 
	 * contents to the registered console device(s).
	 * What happens is : console_init() calls con_init() which in turn
	 * calls register_console() with the argument as console_print.
	 * console_print is a pointer to the function which does the
	 * actual work. And this function is registered to be used by
	 * printk. From then on each printk uses this function to get the
	 * job done immediately - instead of storing the stuff.
	 */
	printk(six_banner);
	/*
	 * Yeah just what it says
	 */ 
	grow_ram();
	/* Exit light enter night Take my hand....Off to never never land */
	start_kernel();	 	
	/* no return */
}
#endif

/* GO!!!!!!!!!!!!!!!!!!!!!!!!!!! */
asmlinkage void start_kernel(void)
{
	static long step = 0;
        char * command_line;
	char *p1, *aa;
	char *t;
/*
 *      This little check will move.
 */
#ifdef __SMP__
        static int first_cpu=1;

        if(!first_cpu)
                start_secondary();
        first_cpu=0;
#endif
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
	/*
	 * Setup arch fills up memory_start and memory_end,
	 * while its at it
	 */
        setup_arch(&command_line, &memory_start, &memory_end);
        memory_start = paging_init(memory_start,memory_end);
	trap_init();
	init_IRQ();
	sched_init();
	time_init();
	parse_options(command_line);
#ifdef CONFIG_MODULES
        init_modules();
#endif
#ifdef CONFIG_PROFILE
        if (!prof_shift)
#ifdef CONFIG_PROFILE_SHIFT
                prof_shift = CONFIG_PROFILE_SHIFT;
#else
                prof_shift = 2;
#endif
#endif
#if (!SIX)
        if (prof_shift) {
                prof_buffer = (unsigned int *) memory_start;
                /* only text is profiled */
                prof_len = (unsigned long) &_etext - (unsigned long) &_stext;
                prof_len >>= prof_shift;
                memory_start += prof_len * sizeof(unsigned int);
        }
#endif
        memory_start = console_init(memory_start,memory_end);
#ifdef CONFIG_PCI
        memory_start = pci_init(memory_start,memory_end);
#endif
        memory_start = kmalloc_init(memory_start,memory_end);
#if 0
	calibrate_delay();
#endif
	memory_start = inode_init(memory_start,memory_end);
	memory_start = file_table_init(memory_start,memory_end);
        memory_start = name_cache_init(memory_start,memory_end);
#ifdef CONFIG_BLK_DEV_INITRD
        if (initrd_start && initrd_start < memory_start) {
                printk(KERN_CRIT "initrd overwritten (0x%08lx < 0x%08lx) - "
                    "disabling it.\n",initrd_start,memory_start);
                initrd_start = 0;
        }
#endif
        mem_init(memory_start,memory_end);
	buffer_init();
	sock_init();
	t = vmalloc(8192);
	vfree(t);

	init_task.kernel_stack_page = alloc_kernel_stack();
#if (SIX)
        init_task.nsp = init_task.kernel_stack_page + DEFAULT_STACK_SIZE;
#if (!__i386__)
        /*
         * Leave room for a sparc stack frame
         */
        init_task.nsp -= 96;
#else
        init_task.nsp -= 4;
#endif
#endif

#if defined(CONFIG_SYSVIPC) || defined(CONFIG_KERNELD)
#if (!SIX)
        ipc_init();
#endif
#endif
        dquot_init();
	arch_syms_export();
	check_bugs();

	printk(linux_banner);
#ifdef __SMP__
#if (!SIX)
        smp_init();
#endif
#endif
	sysctl_init();

#if (SIX)
        if (single)
                single_mode();
#endif
	/*
 	 * Say your prayers little one
 	 */
	sti();

        /*
         *      We count on the initial thread going ok
         *      Like idlers init is an unlocked kernel thread, which will
         *      make syscalls (and thus be locked).
         */
	kernel_thread(init, NULL, NULL);
/*
 * task[0] is meant to be used as an "idle" task: it may not sleep, but
 * it might do some general things like count free pages or it could be
 * used to implement a reasonable LRU algorithm for the paging routines:
 * anything that can be useful, but shouldn't take time from the real
 * processes.
 *
 * Right now task[0] just does a infinite idle loop.
 */
	cpu_idle(NULL);
}



static int init(void * unused)
{
	char buf[15];
#ifdef CONFIG_BLK_DEV_INITRD
        int real_root_mountflags;
#endif
	printk("init 1.0 Booting ...\n");	
        /* Launch bdflush from here, instead of the old syscall way. */
        kernel_thread(bdflush, NULL, 0);
        /* Start the background pageout daemon. */
        kernel_thread(kswapd, NULL, 0);

#ifdef CONFIG_BLK_DEV_INITRD
#if (!SIX)
        real_root_dev = ROOT_DEV;
        real_root_mountflags = root_mountflags;
        if (initrd_start && mount_initrd) root_mountflags &= ~MS_RDONLY;
        else mount_initrd =0;
#endif
#endif
#if (SIX)
        sys_setup();
#else
        setup();
#endif

#ifdef __SMP__
        /*
         *      With the devices probed and setup we can
         *      now enter SMP mode.
         */

        smp_begin();
#endif

#ifdef CONFIG_UMSDOS_FS
        {
                /*
                        When mounting a umsdos fs as root, we detect
                        the pseudo_root (/linux) and initialise it here.
                        pseudo_root is defined in fs/umsdos/inode.c
                */
                extern struct inode *pseudo_root;
                if (pseudo_root != NULL){
                        current->fs->root = pseudo_root;
                        current->fs->pwd  = pseudo_root;
                }
        }
#endif

#ifdef CONFIG_BLK_DEV_INITRD
        root_mountflags = real_root_mountflags;
        if (mount_initrd && ROOT_DEV != real_root_dev && ROOT_DEV == MKDEV(RAMDISK_MAJOR,0)) 		{
                int error;

                pid = kernel_thread(do_linuxrc, "/linuxrc", SIGCHLD);
                if (pid>0)
                        while (pid != wait(&i));
                if (real_root_dev != MKDEV(RAMDISK_MAJOR, 0)) {
                        error = change_root(real_root_dev,"/initrd");
                        if (error)
                                printk(KERN_ERR "Change root to /initrd: "
                                    "error %d\n",error);
                }
        }
#endif

#if (SIX)
        /*
         * Fd 0, 1 and 2 for the first user process.
         *
         * Stock Linux ignores the result here; SIX must not.  If /dev/tty1
         * is missing, or carries a character major that nobody registered,
         * the open fails and /bin/sh is started with no descriptors at all.
         * Every write(1, ...) it makes then returns -EBADF and the shell
         * looks dead while in fact running perfectly.  Say so instead.
         */
        {
                int cfd = sys_open("/dev/tty1", O_RDWR, 0);

                if (cfd < 0)
                        printk("init: cannot open /dev/tty1 (%d) -- "
                               "the first process will have no stdin, "
                               "stdout or stderr\n", cfd);
                else {
                        (void) sys_dup(cfd);
                        (void) sys_dup(cfd);
                }
        }
#else
        (void) open("/dev/tty1",O_RDWR, 0);
        (void) dup(0);
        (void) dup(0);
#endif
        if (!execute_command) {
#if (SIX)
                /*
                 * Both architectures start /etc/init now.
                 *
                 * x86 used to exec /bin/sh directly, because for most of
                 * this port's life /bin/sh was interp.c -- a while loop
                 * that execve()s whatever you type -- and there was no
                 * working init, getty or login to hand.  The real Bourne
                 * shell builds on x86 now, so the SPARC boot sequence
                 * works here too:
                 *
                 *     /etc/init  reads /etc/ttytab
                 *                forks "sh /etc/rc"
                 *                spawns getty on each listed line
                 *     getty  ->  login  ->  /bin/sh
                 *
                 * The return value is reported: sys_execve() only returns
                 * at all when it has failed, and a silent failure here is
                 * indistinguishable from a system that booted and printed
                 * nothing.
                 */
                int ret = sys_execve("/etc/init", argv_init, envp_init);
                printk("init: exec of /etc/init returned %d\n", ret);
#else
                execve("/etc/init", argv_init, envp_init);
#endif
	}
}

#if (SIX)
void get_kernel_mask(so_sigset_t *mask)
{
    static so_sigset_t lockmask;
    static int called_before = 0;

    if (!called_before) {
        sosigfullset(&lockmask);
        sosigdelset(&lockmask, SIGILL);
        sosigdelset(&lockmask, SIGTRAP);
        sosigdelset(&lockmask, SIGEMT);
        sosigdelset(&lockmask, SIGFPE);
        sosigdelset(&lockmask, SIGBUS);
        sosigdelset(&lockmask, SIGSEGV);
        sosigdelset(&lockmask, SIGSYS);
        sosigdelset(&lockmask, SIGINT);
        sosigdelset(&lockmask, SIGLWP);
        called_before = 1;
    }
    *mask = lockmask;
}
#endif

