#ifndef _SYS_SIXCALL_H
#define _SYS_SIXCALL_H

/*
 * The guest's view of the SIX system call interface.
 *
 * This is deliberately a thin wrapper rather than a copy.  The argument
 * block is ABI between two separately compiled halves of the system -- the
 * kernel, built for the host, and this libc, built for the emulated
 * machine -- and a duplicated struct definition is exactly the kind of
 * thing that drifts silently and then costs a week.  So both sides include
 * the same file.
 *
 * The relative paths are ugly, but the guest build uses -nostdinc with only
 * library/include on the search path, and it has no business acquiring the
 * whole kernel include tree just for two declarations.
 */

#include "../../../include/asm-six/sixcall.h"
#include "../../../arch/six/kernel/host.h"

/*
 * The signal that means "system call".  It is a host signal number, not a
 * guest one: the guest raises it with a real Linux kill(2), and SIX's
 * sun_handler() catches it.  See arch/six/kernel/host.h for why it is
 * SIGRTMIN+4 and not something more obvious.
 */
#define SIX_TRAPSIG     SIX_HOST_TRAPSIG

extern int syscall(int num, long one, long two, long three);

#endif /* _SYS_SIXCALL_H */
