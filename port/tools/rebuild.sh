#!/bin/bash
#
# rebuild.sh -- guaranteed-consistent rebuild of the SIX kernel.
#
# Why this exists:
#
# The Linux 2.0 build has no header dependency tracking out of the box
# (Rules.make now adds gcc -MMD, but that only helps once the .d files
# exist, and it cannot help at all for the very first build after a header
# is added).  That matters more here than in most trees, because two of
# SIX's headers control the layout of structures that nearly every object
# touches:
#
#   include/asm-six/ptrace.h  -- struct pt_regs, embedded *twice* in
#                                struct task_struct
#   include/asm-six/irq.h     -- NR_IRQS, which sizes an array in the
#                                middle of struct kernel_stat, so changing
#                                it moves every field after it
#
# A partial rebuild after touching either one produces a single binary
# whose objects disagree about where fields live.  That has already cost
# real debugging time once: sun_handler() switched to a stack pointer of
# zero, which looks like a wild pointer bug and is actually a build bug.
#
# Usage:  port/tools/rebuild.sh [--quiet]
#
set -u

# port/tools/ lives inside the source root, so two levels up is the root.
cd "$(dirname "$0")/../.." || exit 1

QUIET=0
[ "${1:-}" = "--quiet" ] && QUIET=1

# include/asm is a symlink to asm-six and is gitignored; a fresh clone
# will not have it, and nothing builds without it.
if [ ! -e include/asm ]; then
	echo "creating include/asm -> asm-six symlink"
	ln -sfn asm-six include/asm
fi

echo "removing kernel objects"
find . -name '*.o' -not -path './CVS/*' -delete
find . -name '.*.o.d' -delete
find . -path './library/*/lib/*.a' -delete
rm -f six

# Guest programs are build products too.  They used to be left alone here,
# which was a mistake: the disk image is generated from them, so a "full
# rebuild" that skipped them could still produce an image full of binaries
# compiled against headers that no longer exist.
echo "removing guest binaries"
for d in applications/*/; do
	name=$(basename "$d")
	[ -f "$d/$name" ] && rm -f "$d/$name"
done
rm -f applications/hello/hello

# The root filesystem image is a build product like everything else; a full
# rebuild regenerates it.  (It used to be a blob checked into CVS in 2005.)
echo "removing the generated disk image"
rm -f disk/x86/root
rm -rf port/image/.stage

LOG=port/log/rebuild.log
mkdir -p port/log

# SIX_USERLAND=y builds library/ and applications/.  They are not in the
# default SUBDIRS because for most of the port's life they did not compile;
# they do now, and the image cannot be built without them.
if [ "$QUIET" = 1 ]; then
	make SIX_USERLAND=y > "$LOG" 2>&1
else
	make SIX_USERLAND=y 2>&1 | tee "$LOG"
fi

errors=$(grep -c 'error:' "$LOG")
built=$(grep -c ' -c -o ' "$LOG")

echo
echo "objects compiled : $built"
echo "errors           : $errors"

if [ -x ./six ]; then
	echo "image            : $(ls -l six | awk '{print $5}') bytes"
	file six | sed 's/^/                   /'
else
	echo "image            : NOT BUILT"
	exit 1
fi

[ "$errors" -eq 0 ] || exit 1
exit 0
