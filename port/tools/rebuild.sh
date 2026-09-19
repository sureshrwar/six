#!/bin/bash
#
# rebuild.sh -- guaranteed-clean full rebuild of SIX (kernel, guest libc,
# guest applications, and disk/x86/root).
#
# Now that the top-level Makefile's "clean" and default "all" targets handle
# the guest userland, header dependency files, and disk image directly, this
# script is simply a thin wrapper around "make clean && make".
#
# Usage:  port/tools/rebuild.sh [--quiet]
#
set -u

cd "$(dirname "$0")/../.." || exit 1

QUIET=0
[ "${1:-}" = "--quiet" ] && QUIET=1

LOG=port/log/rebuild.log
mkdir -p port/log

make clean >/dev/null 2>&1 || exit 1

if [ "$QUIET" = 1 ]; then
	make > "$LOG" 2>&1
else
	make 2>&1 | tee "$LOG"
fi

errors=$(grep -c 'error:' "$LOG")
built=$(grep -c ' -c -o ' "$LOG")

echo
echo "objects compiled : $built"
echo "errors           : $errors"

if [ -x ./six ] && [ -s disk/x86/root ]; then
	echo "kernel           : $(ls -l six | awk '{print $5}') bytes"
	echo "disk image       : $(ls -l disk/x86/root | awk '{print $5}') bytes"
	file six | sed 's/^/                   /'
else
	echo "build            : INCOMPLETE"
	exit 1
fi

[ "$errors" -eq 0 ] || exit 1
exit 0
