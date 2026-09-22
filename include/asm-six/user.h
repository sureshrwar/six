#ifndef _I386_USER_H
#define _I386_USER_H

#include <asm/page.h>
#include <linux/ptrace.h>

struct user_i387_struct {
	long	cwd;
	long	swd;
	long	twd;
	long	fip;
	long	fcs;
	long	foo;
	long	fos;
	long	st_space[20];
};

struct user {
	struct pt_regs regs;
	int u_fpvalid;
	struct user_i387_struct i387;
	unsigned long int u_tsize;
	unsigned long int u_dsize;
	unsigned long int u_ssize;
	unsigned long start_code;
	unsigned long start_stack;
	long int signal;
	int reserved;
	struct pt_regs * u_ar0;
	struct user_i387_struct * u_fpstate;
	unsigned long magic;
	char u_comm[32];
	int u_debugreg[8];
};

#define NBPG			PAGE_SIZE
#define UPAGES			1
#define HOST_TEXT_START_ADDR	(u.start_code)
#define HOST_STACK_END_ADDR	(u.start_stack + u.u_ssize * NBPG)

#endif /* _I386_USER_H */
