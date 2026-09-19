

char **envlist = 0;
extern void main(int, char **, char **);

cstart(int argc, char *argv[], char *envp[])
{
	envlist = envp;
	main(argc, argv, envp);
	exit(0);
}
