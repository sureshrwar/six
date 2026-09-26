
#ifndef _LINUX_PTRACE_H
#define _LINUX_PTRACE_H
/* ptrace.h */
/* structs and defines to help the user use the ptrace system call. */

#define PTRACE_TRACEME		   0
#define PTRACE_PEEKTEXT		   1
#define PTRACE_PEEKDATA		   2
#define PTRACE_PEEKUSR		   3
#define PTRACE_POKETEXT		   4
#define PTRACE_POKEDATA		   5
#define PTRACE_POKEUSR		   6
#define PTRACE_CONT		   7
#define PTRACE_KILL		   8
#define PTRACE_SINGLESTEP	   9
#define PTRACE_GETREGS		  12
#define PTRACE_SETREGS		  13
#define PTRACE_ATTACH		  16
#define PTRACE_DETACH		  17
#define PTRACE_SYSCALL		  24

/* SIX-specific ptrace helpers for bulk memory access & host debug/source lookup */
#define PTRACE_SIX_READMEM	 100
#define PTRACE_SIX_WRITEMEM	 101
#define PTRACE_SIX_GET_PROC	 102
#define PTRACE_SIX_LOAD_DBG	 103
#define PTRACE_SIX_SRCLINE	 104

struct user_regs_struct {
	unsigned long ebx;
	unsigned long ecx;
	unsigned long edx;
	unsigned long esi;
	unsigned long edi;
	unsigned long ebp;
	unsigned long eax;
	unsigned long ds;
	unsigned long es;
	unsigned long fs;
	unsigned long gs;
	unsigned long orig_eax;
	unsigned long eip;
	unsigned long cs;
	unsigned long eflags;
	unsigned long esp;
	unsigned long ss;
};

struct six_ptrace_mem_req {
	unsigned long addr;
	void *buf;
	int len;
};

struct six_ptrace_proc_info {
	int pid;
	int ppid;
	int state;
	char comm[32];
	char exe_path[128];
	unsigned long start_code;
	unsigned long end_code;
	unsigned long start_data;
	unsigned long end_data;
	unsigned long start_brk;
	unsigned long brk;
	unsigned long start_stack;
};

struct six_dbg_sym {
	unsigned long addr;
	unsigned long size;
	char type;
	char name[55];
};

struct six_dbg_line {
	unsigned long addr;
	unsigned short line;
	unsigned short file_idx;
};

struct six_ptrace_dbg_req {
	const char *exe_path;
	struct six_dbg_sym *syms;
	int max_syms;
	int num_syms;
	struct six_dbg_line *lines;
	int max_lines;
	int num_lines;
	char (*files)[96];
	int max_files;
	int num_files;
};

struct six_ptrace_srcline_req {
	const char *file;
	int line;
	char *buf;
	int buflen;
};

#include <asm/ptrace.h>

#endif


