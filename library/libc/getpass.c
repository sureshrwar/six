/*	getpass() - read a password		Author: Kees J. Bot
 *							Feb 16 1993
 */
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/signal.h>
#include <linux/termios.h>

static int intr;

static void catch(int sig)
{
	intr= 1;
}

char *getpass(const char *prompt)
{
	struct sigaction osa, sa;
	struct termios cooked, raw;
	static char password[32+1];
	int fd, n= 0;

	/* Try to open the controlling terminal. */
	//if ((fd= open("/dev/tty", O_RDONLY)) < 0) return NULL;
	fd = 0;

	/* Trap interrupts unless ignored. */
	intr= 0;
	sigaction(SIGINT, NULL, &osa);
	if (osa.sa_handler != SIG_IGN) {
		sigemptyset(&sa.sa_mask);
		sa.sa_flags= 0;
		sa.sa_handler= catch;
		sigaction(SIGINT, &sa, &osa);
	}

	/* Set the terminal to non-echo mode. */
	tcgetattr(fd, &cooked);
	raw= cooked;
	raw.c_iflag|= ICRNL;
	raw.c_lflag&= ~ECHO;
	raw.c_lflag|= ECHONL;
	raw.c_oflag|= OPOST | ONLCR;
	tcsetattr(fd, &raw);

	/* Print the prompt.  (After setting non-echo!) */
	write(2, prompt, strlen(prompt));

	/* Read the password, 32 characters max. */
	while (read(fd, password+n, 1) > 0) {
		if (password[n] == '\n') break;
		if (n < 32) n++;
	}
	password[n]= 0;

	/* Terminal back to cooked mode. */
	tcsetattr(fd, &cooked);

	//close(fd);

	/* Interrupt? */
	sigaction(SIGINT, &osa, NULL);
	if (intr) raise(SIGINT);

	return password;
}
