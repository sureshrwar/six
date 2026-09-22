#!/bin/bash
#
# mkimage.sh -- build the SIX guest root filesystem image from source.
#
# ---------------------------------------------------------------------------
# Why this exists
# ---------------------------------------------------------------------------
# Until now disk/x86/root was a 5 MB binary blob checked into CVS in 2005.
# Nobody could say with certainty what was inside it, the binaries in it had
# no traceable relationship to the source tree that sits next to them, and
# every change to the guest userland required hand-editing the blob with the
# kernel's own built-in "six single" shell.  It has been deleted and
# untracked.  (It is preserved for archaeology in git tag "v2005-cvs", along
# with the big-endian SPARC image, if anyone ever wants to diff against it.)
#
# This script replaces it.  It reads port/image/manifest.txt, assembles a
# staging tree, and turns that tree into an ext2 image.
#
# ---------------------------------------------------------------------------
# Why fakeroot, and why revision 0
# ---------------------------------------------------------------------------
# Two constraints shape the implementation:
#
# 1. The image needs root-owned files and two character device nodes, and we
#    refuse to require sudo to build a hobby kernel.  fakeroot(1) intercepts
#    the relevant libc calls so that chown(0,0) and mknod() appear to succeed
#    and are remembered; mke2fs, run inside the same fakeroot session, then
#    sees the tree as root would.  Nothing privileged actually happens.
#
# 2. The consumer is the ext2 driver from Linux 2.0.11 (fs/ext2/).  It
#    predates every ext2 feature flag that modern mke2fs turns on by default
#    -- filetype, sparse_super, dir_index, resize_inode, ext_attr, 256-byte
#    inodes, and so on.  So we ask for exactly what the 2005 image was, which
#    dumpe2fs reports as:
#
#        Filesystem revision #:  0 (original)
#        Filesystem features:    (none)
#        Block size:             1024
#        Inode count:            1280
#        Block count:            5120
#
#    -r 0 -O none -b 1024 reproduces that.  Revision 0 also pins the inode
#    size at 128 bytes, which is what the old driver assumes.
#
# ---------------------------------------------------------------------------
# Usage
# ---------------------------------------------------------------------------
#   port/image/mkimage.sh [--strict] [--out PATH] [--manifest PATH]
#
#   --strict    treat a missing source binary as an error.  Without it,
#               missing binaries are warned about and skipped, which is what
#               makes this usable while the guest userland is still being
#               ported one program at a time.
#
# Exit status is 0 on success.
#

set -u

SRCROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$SRCROOT" || exit 1

MANIFEST="port/image/manifest.txt"
OUT="disk/x86/root"
STAGE="port/image/.stage"
STRICT=0

while [ $# -gt 0 ]; do
	case "$1" in
	--strict)   STRICT=1 ;;
	--out)      OUT="$2"; shift ;;
	--manifest) MANIFEST="$2"; shift ;;
	*) echo "mkimage: unknown option $1" >&2; exit 2 ;;
	esac
	shift
done

# Geometry: 50 MB ext2 image (51200 x 1 KB blocks, 12800 inodes).
# Can be overridden via environment variables BLOCK_COUNT and INODE_COUNT.
BLOCK_SIZE=1024
BLOCK_COUNT=${BLOCK_COUNT:-51200}
INODE_COUNT=${INODE_COUNT:-$((BLOCK_COUNT / 4))}

[ -r "$MANIFEST" ] || { echo "mkimage: no manifest at $MANIFEST" >&2; exit 1; }

for tool in fakeroot mke2fs; do
	command -v "$tool" >/dev/null 2>&1 || {
		echo "mkimage: $tool not found -- install it (Debian: e2fsprogs, fakeroot)" >&2
		exit 1
	}
done

# ---------------------------------------------------------------------------
# Pass 1 (outside fakeroot): resolve the manifest, report what is missing,
# and refuse anything that is not a 32-bit little-endian x86 ELF.
#
# That last check is not paranoia.  Every applications/*/ directory in this
# tree contained a *SPARC big-endian* executable last built in 2005, sitting
# right where the Makefile puts its output.  Without this check a build on
# x86 would happily copy them into the image, and the first symptom would be
# an unexplained failure deep inside the guest ELF loader.  The binaries have
# since been deleted and untracked, but the class of mistake is easy to make
# again, so the check stays.
#
# Doing all of this before fakeroot means the user sees one clean list rather
# than having it interleaved with the staging output.
# ---------------------------------------------------------------------------
missing=0
present=0
badarch=0
while read -r type path mode a b c; do
	case "$type" in ''|\#*) continue ;; esac
	[ "$type" = "file" ] || continue
	if [ ! -f "$a" ]; then
		missing=$((missing + 1))
		echo "mkimage: MISSING  $a  (would become $path)" >&2
		continue
	fi
	present=$((present + 1))

	# Only ELF files are checked; /etc/x86.txt is plain text.
	if [ "$(head -c 4 "$a" | od -An -tx1 | tr -d ' ')" = "7f454c46" ]; then
		desc="$(LC_ALL=C file -b "$a")"
		case "$desc" in
		*"ELF 32-bit LSB"*80386* | *"ELF 32-bit LSB"*i386*)
			;;
		*)
			badarch=$((badarch + 1))
			echo "mkimage: WRONG ARCH  $a" >&2
			echo "mkimage:              expected 32-bit LSB Intel 80386, got: $desc" >&2
			;;
		esac
	fi
done < <(sed 's/#.*//' "$MANIFEST")

if [ "$badarch" -gt 0 ]; then
	echo "mkimage: $badarch file(s) are for the wrong architecture; refusing to build" >&2
	exit 1
fi

if [ "$missing" -gt 0 ] && [ "$STRICT" = 1 ]; then
	echo "mkimage: $missing source file(s) missing and --strict was given" >&2
	exit 1
fi

# ---------------------------------------------------------------------------
# Pass 2: build the staging tree and the image, all inside one fakeroot
# session.  It has to be one session: the fake ownership and device numbers
# live in fakeroot's in-memory database, so if mke2fs ran outside it the
# nodes and the root ownership would simply not be there.
# ---------------------------------------------------------------------------
rm -rf "$STAGE"
mkdir -p "$STAGE" "$(dirname "$OUT")"

export SRCROOT STAGE MANIFEST OUT BLOCK_SIZE BLOCK_COUNT INODE_COUNT

fakeroot -- bash -s <<'FAKEROOT_SCRIPT'
set -u
rc=0

while read -r type path mode a b c; do
	case "$type" in ''|\#*) continue ;; esac

	dest="$STAGE$path"

	case "$type" in
	dir)
		mkdir -p "$dest"           || rc=1
		chmod "$mode" "$dest"      || rc=1
		chown 0:0 "$dest"          || rc=1
		;;
	file)
		[ -f "$a" ] || continue    # already reported in pass 1
		mkdir -p "$(dirname "$dest")"
		cp -f "$a" "$dest"         || rc=1
		if [ "$(head -c 4 "$dest" | od -An -tx1 | tr -d ' ')" = "7f454c46" ]; then
			case "$dest" in
			*.o|*.a) ;;
			*) strip --strip-unneeded "$dest" 2>/dev/null || true ;;
			esac
		fi
		chmod "$mode" "$dest"      || rc=1
		chown 0:0 "$dest"          || rc=1
		;;
	node)
		mkdir -p "$(dirname "$dest")"
		# $a = c|b, $b = major, $c = minor
		mknod -m "$mode" "$dest" "$a" "$b" "$c" || rc=1
		chown 0:0 "$dest"          || rc=1
		;;
	sym)
		mkdir -p "$(dirname "$dest")"
		ln -sfn "$a" "$dest"       || rc=1
		;;
	*)
		echo "mkimage: unknown manifest type '$type'" >&2
		rc=1
		;;
	esac
done < <(sed 's/#.*//' "$MANIFEST")

chown 0:0 "$STAGE"
chmod 0755 "$STAGE"

[ "$rc" = 0 ] || { echo "mkimage: staging failed" >&2; exit 1; }

rm -f "$OUT"

# -q        quiet
# -F        don't complain that the target is a plain file
# -t ext4   build a real ext4 filesystem, read by SIX's fs/ext4 driver
# -b 1024   block size, matching the 2005 image
# -N 1280   inode count, matching the 2005 image
# -I 256    256-byte inodes.  ext4 needs these for i_extra_isize (the driver
#           writes extra_isize = 32); they also leave room for the nanosecond
#           and crtime fields that debugfs shows.
# -m 5      5% reserved, matching the 256-of-5120 blocks the 2005 image had
# -U        a fixed UUID, so two builds of the same tree are identical
# -d        populate from the staging tree
#
# The three features we must turn OFF, and why:
#   ^metadata_csum  SIX has no crc32c implementation, so it can neither verify
#                   nor maintain group-descriptor/inode/extent checksums.  A
#                   single guest write would leave every touched structure
#                   with a stale checksum and e2fsck would reject the image.
#   ^64bit          64bit widens group descriptors from 32 to 64 bytes and adds
#                   the *_hi halves.  SIX is strictly 32-bit and its
#                   ext2_group_desc is the 32-byte layout.
#   ^orphan_file    a recent INCOMPAT feature; an unknown incompat bit makes
#                   ext4_read_super() refuse the mount outright.
#
# Everything else mke2fs enables by default is kept, and is genuinely
# exercised by the driver: extents (the B+tree in fs/ext4/extents.c),
# flex_bg (which is why ext4_check_descriptors() relaxes its per-group
# containment check), dir_index, sparse_super, filetype, large_file,
# huge_file, dir_nlink and extra_isize.
#
# Note there is no longer any revision demotion here.  The old ext2 image was
# demoted to revision 0 with "debugfs -w -R 'ssv rev_level 0'" because the 2.0
# driver only understood the original layout; ext4 requires revision 1 (it is
# where s_inode_size, s_first_ino and the feature masks live), so that step
# has been removed.  e2fsck -fn afterwards confirms the result is coherent.
mke2fs -q -F \
	-t ext4 \
	-b "$BLOCK_SIZE" \
	-N "$INODE_COUNT" \
	-I 256 \
	-O ^metadata_csum,^64bit,^orphan_file -m 5 \
	-U 13bcf00c-78b2-11d9-8fdf-f213c4292cfb \
	-d "$STAGE" \
	"$OUT" "$BLOCK_COUNT"

[ -s "$OUT" ] || { echo "mkimage: mke2fs produced nothing" >&2; exit 1; }
FAKEROOT_SCRIPT

rc=$?
[ "$rc" = 0 ] || { echo "mkimage: failed" >&2; exit "$rc"; }

# The image is created "not clean" by mke2fs only if something went wrong;
# mark it cleanly checked so the kernel's mount does not print the
# "mounting unchecked fs" warning on every boot.
if command -v tune2fs >/dev/null 2>&1; then
	tune2fs -c 0 -i 0 "$OUT" >/dev/null 2>&1
fi

# Reproducibility: mke2fs stamps the current time into the superblock.  If
# SOURCE_DATE_EPOCH is set, rewrite the timestamps so that identical inputs
# give a byte-identical image.
if [ -n "${SOURCE_DATE_EPOCH:-}" ] && command -v debugfs >/dev/null 2>&1; then
	debugfs -w -R "ssv mtime @$SOURCE_DATE_EPOCH" "$OUT" >/dev/null 2>&1
	debugfs -w -R "ssv wtime @$SOURCE_DATE_EPOCH" "$OUT" >/dev/null 2>&1
	debugfs -w -R "ssv lastcheck @$SOURCE_DATE_EPOCH" "$OUT" >/dev/null 2>&1
fi

echo "mkimage: wrote $OUT ($(stat -c %s "$OUT") bytes, $present file(s), $missing missing)"
exit 0
