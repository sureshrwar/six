#ifndef _SYS_PTRACE_H
#define _SYS_PTRACE_H

#include <linux/ptrace.h>

extern long ptrace(long request, long pid, long addr, long data);

#endif /* _SYS_PTRACE_H */
