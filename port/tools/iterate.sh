#!/bin/bash
# iterate.sh -- build, auto-repair the mechanical errors, repeat.
#
# The GCC-15 fallout in this tree is dominated by one bug class
# (use-before-declaration).  add_fwd_decls.py fixes those from the
# compiler's own diagnostics, so build/fix/rebuild converges quickly.
# This loop runs that cycle until it stops making progress, then hands
# whatever is left to a human.
#
# Usage: port/tools/iterate.sh [max_rounds]

set -u
cd "$(dirname "$0")/../.." || exit 1
ROOT=$(pwd)
MAX=${1:-8}
mkdir -p port/log

prev=-1
for i in $(seq 1 "$MAX"); do
    log="port/log/iter$(printf '%02d' "$i").log"
    make -k six > "$log" 2>&1
    errs=$(grep -cE ': error:' "$log")
    objs=$(find . -name '*.o' | wc -l)
    dir=$(grep -E 'Entering directory' "$log" | sed "s|.*/six/||" | tail -1)

    printf 'round %d: %3d errors, %3d objects, reached %s\n' \
           "$i" "$errs" "$objs" "${dir:-<top>}"

    if [ "$errs" -eq 0 ]; then
        echo "=> no compile errors remaining"
        exit 0
    fi
    if [ "$errs" -eq "$prev" ]; then
        echo "=> no progress this round; remaining errors need a human:"
        grep -oE ': error: .*' "$log" \
          | sed -E "s/'[^']*'/'X'/g; s/[0-9]+/N/g" \
          | sort | uniq -c | sort -rn | head -15
        exit 1
    fi
    prev=$errs

    python3 port/tools/add_fwd_decls.py "$log" --root "$ROOT" 2>&1 | tail -3
done

echo "=> hit max rounds ($MAX)"
exit 1
