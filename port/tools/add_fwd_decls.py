#!/usr/bin/env python3
"""
add_fwd_decls.py -- mechanically repair "used before defined" static functions.

GCC 2.x happily synthesised an implicit `int ()` declaration at the first
call site of a not-yet-declared function.  When the real definition turned
up later in the file marked `static`, the old compiler shrugged.  GCC 14+
rejects this as:

    error: static declaration of 'foo' follows non-static declaration

The SIX tree (Linux 2.0.11) does this in dozens of places.  The fix is
always identical and always safe: hoist a forward declaration of the
function to the top of the file.  This script does that, driven by the
compiler's own diagnostics, so it can only touch functions GCC actually
complained about.

Usage:
    add_fwd_decls.py <build.log> [--root DIR] [--dry-run]

It is idempotent: re-running will not insert duplicate declarations.
"""

import argparse
import os
import re
import sys

# console.c:1436:20: error: static declaration of 'run_timer_list' follows ...
ERR_RE = re.compile(
    r"^(?P<file>[^\s:]+):(?P<line>\d+):\d+: error: static declaration of "
    r"[\u2018'\"](?P<name>\w+)[\u2019'\"] follows non-static declaration"
)

# The non-static flavour of the same bug.  A call ahead of any declaration
# gets an implicit `int ()`, which then clashes with a definition returning
# a pointer or a narrow type:
#
#   keyboard.c:123:15: error: conflicting types for 'screenpos'; have '...'
#   ...: note: previous implicit declaration of 'screenpos' with type 'int()'
#
# We only act when the accompanying note says "previous implicit
# declaration", so genuine type mismatches are left alone for a human.
CONFLICT_RE = re.compile(
    r"^(?P<file>[^\s:]+):(?P<line>\d+):\d+: error: conflicting types for "
    r"[\u2018'\"](?P<name>\w+)[\u2019'\"]"
)
IMPLICIT_NOTE_RE = re.compile(
    r"note: previous implicit declaration of [\u2018'\"](?P<name>\w+)[\u2019'\"]"
)

# Map an object/source basename back to a real path. GCC prints the path it
# was given, which for the recursive SIX make is just the bare filename.
def resolve(root, path, make_dirs):
    if os.path.isabs(path) and os.path.exists(path):
        return path
    cand = os.path.join(root, path)
    if os.path.exists(cand):
        return cand
    base = os.path.basename(path)
    for d in make_dirs:
        cand = os.path.join(d, base)
        if os.path.exists(cand):
            return cand
    return None


def collect_make_dirs(log_text, root):
    """Every directory make announced entering -- used to resolve bare names."""
    dirs = []
    for m in re.finditer(r"Entering directory '([^']+)'", log_text):
        if m.group(1) not in dirs:
            dirs.append(m.group(1))
    dirs.append(root)
    return dirs


def extract_signature(lines, idx):
    """
    Given the 0-based index of a function definition line, return the text
    from there up to and including the closing ')' of the parameter list.
    Handles definitions that wrap across several lines.
    """
    buf = ""
    depth = 0
    seen_open = False
    for i in range(idx, min(idx + 12, len(lines))):
        for ch in lines[i]:
            if ch == "{" and not seen_open:
                # K&R style or body started before we found a paren: give up
                return None
            buf += ch
            if ch == "(":
                depth += 1
                seen_open = True
            elif ch == ")":
                depth -= 1
                if seen_open and depth == 0:
                    return " ".join(buf.split())
        buf += " "
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--root", default=".")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    with open(args.log, errors="replace") as f:
        log = f.read()

    make_dirs = collect_make_dirs(log, root)

    # file -> {name: (defline, is_static)}
    todo = {}
    unresolved = []
    loglines = log.splitlines()
    for i, line in enumerate(loglines):
        stripped = line.strip()
        is_static = None

        m = ERR_RE.match(stripped)
        if m:
            is_static = True
        else:
            m = CONFLICT_RE.match(stripped)
            if m:
                # Only treat this as a hoistable case if GCC says the prior
                # declaration was implicit.  A genuine signature mismatch
                # between two real declarations needs human judgement.
                window = "\n".join(loglines[i:i + 8])
                note = IMPLICIT_NOTE_RE.search(window)
                if not note or note.group("name") != m.group("name"):
                    continue
                is_static = False

        if is_static is None:
            continue

        path = resolve(root, m.group("file"), make_dirs)
        if not path:
            unresolved.append(m.group("file"))
            continue
        todo.setdefault(path, {})[m.group("name")] = (int(m.group("line")),
                                                      is_static)

    if not todo:
        print("no hoistable use-before-declaration errors found")
        if unresolved:
            print("  (could not resolve: %s)" % ", ".join(sorted(set(unresolved))))
        return 0

    MARKER = "/* Forward declarations hoisted for modern GCC"

    total = 0
    for path, names in sorted(todo.items()):
        with open(path, errors="replace") as f:
            lines = f.readlines()

        protos = []
        skipped = []
        for name, (defline, is_static) in sorted(names.items(),
                                                 key=lambda kv: kv[1][0]):
            sig = extract_signature(lines, defline - 1)
            if not sig:
                skipped.append(name)
                continue
            # Only the static flavour needs the storage class forced; giving
            # a non-static function a static prototype would change linkage.
            if is_static and not sig.lstrip().startswith("static"):
                sig = "static " + sig.lstrip()
            protos.append((name, sig + ";"))

        if not protos:
            print("%s: nothing hoistable (%s)" % (path, ", ".join(skipped)))
            continue

        text = "".join(lines)
        protos = [(n, p) for (n, p) in protos
                  if not re.search(r"^\s*%s\s*$" % re.escape(p), text, re.M)]
        if not protos:
            print("%s: already done" % path)
            continue

        # Insert after the last top-level #include in the first 200 lines.
        ins = 0
        for i, l in enumerate(lines[:200]):
            if l.lstrip().startswith("#include"):
                ins = i + 1
        if ins == 0:
            skipped.append("(no #include found; inserting at top)")

        block = ["\n", MARKER + " -- these statics are\n",
                 " * called earlier in this file than they are defined.  Older gcc\n",
                 " * accepted the resulting implicit declaration; modern gcc does not.\n",
                 " */\n"]
        block += [p + "\n" for (_, p) in protos]
        block.append("\n")

        if args.dry_run:
            print("--- %s (insert at line %d) ---" % (path, ins + 1))
            print("".join(block))
        else:
            lines[ins:ins] = block
            with open(path, "w") as f:
                f.writelines(lines)

        total += len(protos)
        note = ("  [skipped: %s]" % ", ".join(skipped)) if skipped else ""
        print("%s: +%d forward decls%s" % (os.path.relpath(path, root),
                                           len(protos), note))

    print("\ntotal forward declarations added: %d" % total)
    if unresolved:
        print("unresolved files: %s" % ", ".join(sorted(set(unresolved))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
