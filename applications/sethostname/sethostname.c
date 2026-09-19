#include <errno.h>
#include <stdio.h>


main(int argc, char *argv[])
{
	if (argc != 2)
	{
		fprintf(stderr, "usage: sethostname name\n");
		exit(1);
	}
	if (sethostname(argv[1], strlen(argv[1])) <0)
	{
		fprintf(stderr, "failed\n");
		exit(2);
	}
	return 0;	
}
