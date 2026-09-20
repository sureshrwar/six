char **envlist = 0;
extern void main(int, char **, char **);

void cstart(int argc, char *argv[], char *envp[])
{
	envlist = envp;
	main(argc, argv, envp);
	exit(0);
}

void _start(int argc, char *argv[], char *envp[])
{
	cstart(argc, argv, envp);
}
