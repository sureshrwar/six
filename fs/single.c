
/*
 * Some functions for single mode disk updating. Very very crude setup.
 * Am in no mood to worry about proper programming style so am just whipping
 * together stuff and getting them to do what I want them to do. Not that
 * I'm a classy coder or anything, but I can definitely do better.
 */

#include <stdio.h>
#include <ctype.h>


#include <linux/sched.h>
#include <linux/dirent.h>


#define SINGLE_PROMPT	"go> "
#define HELPFILE	"/etc/shelp.txt"
#define READ_BUF_SIZE	1024


/*
 * Bloody ripe for overflow. Don't care though. 
 */
char line[1024];
char pwd[512];

char *arg[4];
int nargs;


void get_line()
{
	int ret;

	arg[0] = arg[1] = arg[2] = arg[3]  = 0;
	nargs = 0;

	while (1)
	{
		memset(line, 0, 1024);
		write(1, SINGLE_PROMPT, 4);
		ret = read(0, line, 1024);
		if (ret == 1)
			continue;
		else if (ret <= 0)
		{
			write(1, "EOF?\n", 5);
			exit(0);
		}
		else
		{
			line[ret-1] = 0;
			return;
		}
	}
}

void failure()
{
	puts("Operation failed");
}

int get_args(char *l, int len)
{
        int i = 0, j = 0, k = 0;
        int flag = 0;

        while (i < len)
        {
                if (isspace(l[i]))
                {
                        l[i] = 0;
                }
                else if (!i || !l[i-1])
                {
                        arg[k] = &l[i];
                        k++;
                }
                i++;
        }
        return k;
}

single_ls()
{
	int fd, ret;
	struct dirent *b;
	char aa[256], *a;

	a = aa;

	/*
	 * Open the current dir.
	 */
	fd = sys_open(".", 0, 0);

	if (fd < 0)
		failure(); 

	while(1)
	{
		int cur = 0;
		ret = sys_getdents(fd, (struct dirent *)a, 256);
		if (!ret) 
			break;
        	while(cur < ret)
        	{
                	b = (struct dirent *)a;
                	printf("%s   ", b->d_name);
                	a += b->d_reclen;
                	cur += b->d_reclen;
        	}
	}
        printf("\n");
	sys_close(fd);
}



void single_mkdir()
{
	if (nargs == 2) 
	{
		if (sys_mkdir(arg[1]))
			failure();
	}
	else puts("usage: mkdir dirname");
}

void single_cp()
{
	if (nargs == 3)
	{
		char ch[READ_BUF_SIZE];
		int fdr, fdw, count = 0, len = 0;	
		if (access(arg[1], 0))
		{
			puts("cant access source file");
			return;
		}

		fdr = open(arg[1], 0);
		fdw = sys_open(arg[2], O_CREAT | O_TRUNC | O_WRONLY);

		if (fdw < 0)
		{
			puts("cant open file for writing");
			return;
		}
	
		if (fdr < 0)
		{
			puts("cant create destination file");
			return;
		}
		
		while ((len = read(fdr, ch, READ_BUF_SIZE)) > 0)
		{
			sys_write(fdw, ch, len);			
			count+=len;
		}
		
		printf("Wrote %d chars.\n", count);
		close(fdr);
		sys_close(fdw);
		sys_chmod(arg[2], 0755);
		sys_sync();
	}
	else puts("usage: cp source dest");
}

void single_help()
{
	int fd;
	char ch;

	if (sys_access(HELPFILE, 0))
	{
		puts("cant locate help files");
		return;
	}
	else
	{
		fd = sys_open("/etc/shelp.txt", O_RDONLY);
		if (fd < 0)
		{
			puts("error opening help file");
			return;
		}
		while (sys_read(fd, &ch, 1)>0)
			putchar(ch);
		sys_close(fd);
	}
}

void single_mknod()
{
	if (nargs == 4)
	{
		int major, minor, dev;

		major = atoi(arg[2]);
		minor = atoi(arg[3]);
		dev = (major << 8) | minor;

		if (sys_mknod(arg[1], S_IFCHR, dev))
			failure();
	}
	else puts("usage: mknod filename major minor");
}

void single_chmod()
{
	if (nargs == 2)
	{
		if (sys_chmod(arg[1], 0755))
			failure();
	}
	else puts("usage: chmod filename");
}

void goup()
{
        int i = strlen(pwd);
        i--;
        if(pwd[i] == '/' && i)
                pwd[i] = 0;
        while(pwd[i] != '/')
                i--;
        if(i)
                pwd[i] = 0;
        else
                pwd[++i] = 0;

}

/*
 * Verryy buggy. Will fix em all later.
 */
void single_cd()
{
        int i = 0, len;
        char *a;
	
	if (nargs == 1)
	{
		sys_chdir("/");
		memset(pwd, 0, 512);
		strcpy(pwd, "/");
	}
	else
	{
        	a = arg[1];
        	if (!strcmp(a, "."))
        		return;
        	len = strlen(a);
        	if (a[len-1] == '/')
                	a[len-1] = 0;
        	if (sys_chdir(a) < 0)
                	failure();
        	else
        	{
                	if(a[0] == '/')
                        	strcpy(pwd, a);
                	else if(a[0] == '.' && a[1] == '.')
                                	goup();
                	else
                	{
                         	if(strlen(pwd) > 1)
                                	strcat(pwd, "/");
                         	strcat(pwd, a);
                	}
        	}
	}
}

void single_rm()
{
	if (nargs == 2) 
	{
		if (sys_unlink(arg[1]) && sys_rmdir(arg[1]))
			failure();
	}
	else
		puts("usage: rm filename");
}

void do_process()
{
	if (!strcmp(arg[0], "ls") || !strcmp(arg[0], "l"))
		single_ls();
	else if (!strcmp(arg[0], "sync") || !strcmp(arg[0], "s"))
		sys_sync();
	else if (!strcmp(arg[0], "mkdir"))
		single_mkdir();
	else if (!strcmp(arg[0], "cp"))
		single_cp();
	else if (!strcmp(arg[0], "mknod"))
		single_mknod();
	else if (!strcmp(arg[0], "chmod"))
		single_chmod();
	else if (!strcmp(arg[0], "cd"))
		single_cd();
	else if (!strcmp(arg[0], "pwd"))
		puts(pwd);
	else if (!strcmp(arg[0], "rm"))
		single_rm();
	else if (!strcmp(arg[0], "help"))
		single_help();
	else
		puts("Command not found");
}

void process_line()
{
	char *comm, *arg1, *arg2;

	int len = strlen(line);
	if (!line[0]) 
		return;

	nargs = get_args(line, len);

	if (nargs >= 1)
		do_process();
}

void single_init() 
{
	/*
	 * These are the basic stuff that we need to do, to manipulate hard disk info.
	 * Basically init ext2, init block device, initialize rand (because we need
	 * this stuff during HD access) and mount the fucking root.
	 */
	init_ext2_fs();
	blk_dev_init();
	rand_initialize();
	mount_root();

        memset(pwd, 0, 256);
	memset(line, 0, 1024);
        strcpy(pwd, "/");
}

void single_mode()
{
	single_init();

	while (1)
	{
		get_line();
		if (!strcmp(line, "q") || !strcmp(line, "exit") || !strcmp(line, "quit"))
			return;
		process_line();
	}
}

