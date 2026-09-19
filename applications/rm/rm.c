
#include <linux/types.h>
#include <linux/dirent.h>

main(int argc, char *argv[], char *envp[])
{
	if (argc != 2)
	{
		printf("usage : %s file\n", argv[0]);
		exit(2);
	}	
	if (rmdir(argv[1]) < 0)
	{
		printf("Operation failed\n");
		exit(1);
	}
}
