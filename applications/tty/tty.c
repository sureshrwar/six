
#include <linux/types.h>
#include <linux/dirent.h>
#include <stat.h>

main(int argc, char *argv[], char *envp[])
{
	char *tty_name = 0;

	tty_name =  ttyname(0);
	if ((argc == 2) && (!strcmp(argv[1], "-s")))
         	/* Do nothing - shhh! we're in silent mode */ ;
  	else
        	puts((tty_name != NULL) ? tty_name : "not a tty");

  	if (isatty(0) == 0)
        	return(1);
  	else
        	return(0);
}
