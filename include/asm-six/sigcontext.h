
#ifndef _ASM_SIX_SIGCONTEXT_H
#define _ASM_SIX_SIGCONTEXT_H

#include <asm/ptrace.h>

/*
 * As documented in the iBCS2 standard..
 *
 * The first part of "struct _fpstate" is just the
 * normal i387 hardware setup, the extra "status"
 * word is used to save the coprocessor status word
 * before entering the handler.
 */
struct _fpreg {
	unsigned short significand[4];
	unsigned short exponent;
};

struct _fpstate {
	unsigned long 	cw,
			sw,
			tag,
			ipoff,
			cssel,
			dataoff,
			datasel;
	struct _fpreg	_st[8];
	unsigned long	status;
};

struct sigcontext {
	unsigned long flags;
	unsigned long mask;
	struct pt_regs oldcon;
};

#endif
