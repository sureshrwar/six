#include <stdio.h>
#include <ctype.h>
#include <errno.h>

#define LINE_MAX	1024
#define PROMPT		"i-am-dumb> "
#define PROMPT_LEN	11
#define ARG_MAX		3

char line[LINE_MAX];
char *arg[5];
int nargs;

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
			if (k > ARG_MAX)
				break;
                }
                i++;
        }
	arg[k] = 0;
        return k;
}

void readline()
{
        int ret;

        arg[0] = arg[1] = arg[2] = arg[3]  = 0;
        nargs = 0;

        while (1)
        {
                memset(line, 0, 1024);
                write(1, PROMPT, PROMPT_LEN);
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
			nargs = get_args(line, ret);
                        return;
                }
        }
}

void failure()
{
        puts("Operation failed");
}

void execute()
{
	int pid;
	char command[LINE_MAX];
	char *paths[] = { "/bin", "/etc" };

	if (!strcmp(arg[0], "exit"))
		exit(0);
	
	if (!(pid = fork()))
	{
		if (arg[0][0] == '/')
		{
			execve(arg[0], arg, envlist);
		}
		else
		{
			int i;
			for (i=0; i<2; i++)
			{
				strcpy(command, paths[i]);
				strcat(command, "/");
				strcat(command, arg[0]);
				execve(command, arg, envlist);
			}
		}
		failure();
	}
	else waitpid(pid, 0, 0);
}

void show_it()
{
	int fd, len = 0;
	char line[512];
	sleep(1);
	if ((fd=open("/etc/x86.txt", 0)) >= 0)
	{
		while((len = read(fd, line, 512)) > 0)
			write(1, line, len);
		close(fd);
	}
}

main(int argc, char *argv[])
{
	show_it();
	while(1)
	{
		readline();
		execute();
	}
}
