/*
 * Does %esi survive from a guest trap to SIX's signal handler?
 *
 * This is the single assumption the whole guest system call design rests
 * on, and it is the direct replacement for an assumption that turned out
 * to be false: the 2005 code passed arguments in MMX registers, and Linux
 * zeroes the FPU state before entering a signal handler, so they arrived
 * as zero.  That was only discovered by measuring it.  Measure this one
 * too.
 *
 * Build and run:
 *      gcc -m32 -fno-pic -o /tmp/esitest port/tools/esi_survives.c && /tmp/esitest
 */

#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>

#define TRAPSIG (SIGRTMIN + 4)

struct six_guest_call {
	unsigned long nr, a1, a2, a3, a4, a5, a6, ret;
};

static volatile unsigned long seen_esi;
static volatile unsigned long seen_nr;
static volatile int handler_ran;

static void handler(int sig, siginfo_t *si, void *uc)
{
	ucontext_t *u = (ucontext_t *) uc;
	struct six_guest_call *gc;

	seen_esi = (unsigned long) u->uc_mcontext.gregs[REG_ESI];

	/* Dereference it the way system_call() will. */
	gc = (struct six_guest_call *) seen_esi;
	seen_nr = gc->nr;

	/* And write a result back, the way system_call() will. */
	gc->ret = 0xd0d0cafe;

	handler_ran = 1;
}

int main(void)
{
	struct sigaction sa;
	struct six_guest_call args;
	int pid;

	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = handler;
	sa.sa_flags = SA_SIGINFO | SA_RESTART;
	sigemptyset(&sa.sa_mask);
	if (sigaction(TRAPSIG, &sa, NULL) != 0) {
		perror("sigaction");
		return 1;
	}

	memset(&args, 0, sizeof args);
	args.nr = 4;            /* __NR_write */
	args.a1 = 0x11111111;
	args.a2 = 0x22222222;
	args.a3 = 0x33333333;

	__asm__ __volatile__ ("int $0x80"
			      : "=a" (pid)
			      : "0"  (20)               /* __NR_getpid */
			      : "memory");

	__asm__ __volatile__ ("int $0x80"
			      :
			      : "a" (37),               /* __NR_kill    */
				"b" (pid),
				"c" (TRAPSIG),
				"S" (&args)
			      : "memory");

	printf("handler ran     : %s\n", handler_ran ? "yes" : "NO");
	printf("&args           : %08lx\n", (unsigned long) &args);
	printf("gregs[REG_ESI]  : %08lx  %s\n", seen_esi,
	       seen_esi == (unsigned long) &args ? "MATCH" : "*** MISMATCH ***");
	printf("args.nr as seen : %lu %s\n", seen_nr,
	       seen_nr == 4 ? "(correct)" : "*** WRONG ***");
	printf("args.ret written: %08lx %s\n", args.ret,
	       args.ret == 0xd0d0cafe ? "(correct)" : "*** WRONG ***");

	if (!handler_ran || seen_esi != (unsigned long) &args ||
	    seen_nr != 4 || args.ret != 0xd0d0cafe)
		return 1;

	printf("\nOK: the %%esi channel works.\n");
	return 0;
}
