/*
 * ucontext_layout.c -- print the word offsets of everything SIX cares about
 * inside the *host's* ucontext_t.
 *
 * SIX's `struct pt_regs` (include/asm-six/ptrace.h) is not a description of
 * a machine's registers at all: it is a byte-exact overlay of the Solaris
 * ucontext_t, written as a list of named fields separated by hand-counted
 * `unsigned int uuN[...]` padding.  Everything in the kernel that says
 * regs->pc or regs->esp is really indexing into a saved host context.
 *
 * Porting that overlay to glibc means recounting the padding.  Rather than
 * doing the arithmetic by hand, build and run this:
 *
 *      gcc -m32 -o /tmp/uclayout port/tools/ucontext_layout.c && /tmp/uclayout
 *
 * and transcribe the numbers it prints.
 */

/* REG_EIP and friends are behind __USE_GNU in glibc's <sys/ucontext.h>. */
#define _GNU_SOURCE

#include <stdio.h>
#include <stddef.h>
#include <signal.h>
#include <ucontext.h>

#define W(f)   ((int)(offsetof(ucontext_t, f) / sizeof(unsigned int)))
#define GW(i)  ((int)((offsetof(ucontext_t, uc_mcontext.gregs) +           \
                       (i) * sizeof(greg_t)) / sizeof(unsigned int)))

int main(void)
{
	printf("sizeof(ucontext_t)      = %d bytes (%d words)\n",
	       (int)sizeof(ucontext_t),
	       (int)(sizeof(ucontext_t) / sizeof(unsigned int)));
	printf("sizeof(greg_t)          = %d\n", (int)sizeof(greg_t));
	printf("sizeof(mcontext_t)      = %d\n", (int)sizeof(mcontext_t));
	printf("sizeof(sigset_t)        = %d\n", (int)sizeof(sigset_t));
	printf("sizeof(stack_t)         = %d\n", (int)sizeof(stack_t));
	printf("\n");
	printf("word  field                       SIX name\n");
	printf("----  --------------------------  ------------\n");
	printf("%4d  uc_flags\n",                 W(uc_flags));
	printf("%4d  uc_link\n",                  W(uc_link));
	printf("%4d  uc_stack.ss_sp              uc_sp\n",      W(uc_stack.ss_sp));
	printf("%4d  uc_stack.ss_flags\n",        W(uc_stack.ss_flags));
	printf("%4d  uc_stack.ss_size            uc_sp_size\n", W(uc_stack.ss_size));
	printf("%4d  uc_mcontext.gregs[0]\n",     GW(0));
	printf("%4d  gregs[REG_EBP]              ebp\n",  GW(REG_EBP));
	printf("%4d  gregs[REG_ESP]\n",                   GW(REG_ESP));
	printf("%4d  gregs[REG_EIP]              pc\n",   GW(REG_EIP));
	printf("%4d  gregs[REG_EFL]              psw\n",  GW(REG_EFL));
	printf("%4d  gregs[REG_UESP]             esp\n",  GW(REG_UESP));
	printf("%4d  gregs[REG_SS]\n",                    GW(REG_SS));
	printf("%4d  uc_mcontext.oldmask\n",      W(uc_mcontext.oldmask));
	printf("%4d  uc_mcontext.cr2\n",          W(uc_mcontext.cr2));
	printf("%4d  uc_mcontext.fpregs\n",       W(uc_mcontext.fpregs));
	printf("%4d  uc_sigmask\n",               W(uc_sigmask));
	printf("\n");
	printf("For reference, the Solaris offsets SIX was written against:\n");
	printf("  uc_sp 6  uc_sp_size 7  ebp 15  pc 23  psw 25  esp 26\n");
	printf("  g2 35  g3 36  g4 40  g5 41  g6 45  g7 46  g8 50  g9 51\n");
	printf("  (g2..g9 landed inside the *embedded* FPU save area, which is\n");
	printf("   how the MMX syscall channel was read back out of the saved\n");
	printf("   context: mm0 aliases ST0 at +0, mm2 aliases ST2 at +20, and\n");
	printf("   so on -- a 20-byte stride, which is why only the even MMX\n");
	printf("   registers were ever used.  glibc stores the FPU state out of\n");
	printf("   line behind uc_mcontext.fpregs, so this trick cannot port.)\n");
	return 0;
}
