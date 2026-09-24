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
# staging tree, and turns that tree into a root filesystem image.
#
# "--fstype ext4" (the default) produces a real ext4 filesystem for the
# fs/ext4 driver; "--fstype ext2" produces the original revision-0 ext2
# filesystem for fs/ext2.  Both come from the same staging tree, so booting
# one and then the other is an A/B test of the two drivers against identical
# contents.  The Makefile wraps these as "make ext4-image" and
# "make ext2-image".
#
# ---------------------------------------------------------------------------
# Why fakeroot, and why revision 0 for ext2
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
MODE="root"

# Which on-disk format to pack the staging tree into.  ext4 is the default;
# ext2 is kept so fs/ext2 stays testable and so the pre-ext4 image can be
# reproduced byte for byte.  See the mke2fs block near the end for what each
# one actually asks for.
FSTYPE=ext4

while [ $# -gt 0 ]; do
	case "$1" in
	--strict)   STRICT=1 ;;
	--out)      OUT="$2"; shift ;;
	--manifest) MANIFEST="$2"; shift ;;
	--fstype)   FSTYPE="$2"; shift ;;
	--mode)     MODE="$2"; shift ;;
	*) echo "mkimage: unknown option $1" >&2; exit 2 ;;
	esac
	shift
done

case "$FSTYPE" in
ext2|ext4) ;;
*) echo "mkimage: --fstype must be ext2 or ext4 (got '$FSTYPE')" >&2; exit 2 ;;
esac

case "$MODE" in
root|bin|full) ;;
*) echo "mkimage: --mode must be root, bin, or full (got '$MODE')" >&2; exit 2 ;;
esac

if [ "$MODE" = "bin" ]; then
	[ "$OUT" = "disk/x86/root" ] && OUT="disk/x86/bin_storage"
	STAGE="port/image/.stage_bin"
	BLOCK_SIZE=1024
	BLOCK_COUNT=${BLOCK_COUNT:-15360}
	INODE_COUNT=${INODE_COUNT:-512}
	SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1700000000}
	export SOURCE_DATE_EPOCH
else
	# Geometry: 50 MB ext2 image (51200 x 1 KB blocks, 12800 inodes).
	# Can be overridden via environment variables BLOCK_COUNT and INODE_COUNT.
	BLOCK_SIZE=1024
	BLOCK_COUNT=${BLOCK_COUNT:-51200}
	INODE_COUNT=${INODE_COUNT:-$((BLOCK_COUNT / 4))}
fi

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
	if [ "$MODE" = "root" ]; then
		case "$path" in /bin/*) continue ;; esac
	elif [ "$MODE" = "bin" ]; then
		case "$path" in /bin/*) ;; *) continue ;; esac
	fi
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

export SRCROOT STAGE MANIFEST OUT BLOCK_SIZE BLOCK_COUNT INODE_COUNT FSTYPE MODE

fakeroot -- bash -s <<'FAKEROOT_SCRIPT'
set -u
rc=0

while read -r type path mode a b c; do
	case "$type" in ''|\#*) continue ;; esac

	if [ "$MODE" = "root" ]; then
		case "$path" in
		/bin/*) continue ;;
		esac
	elif [ "$MODE" = "bin" ]; then
		case "$path" in
		/bin/*) path="${path#/bin}" ;;
		*) continue ;;
		esac
	fi

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

if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
	find "$STAGE" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} + 2>/dev/null || true
fi

[ "$rc" = 0 ] || { echo "mkimage: staging failed" >&2; exit 1; }

rm -f "$OUT"

LABEL_OPT=""
if [ "$MODE" = "bin" ]; then
	LABEL_OPT="-L bin_verity"
else
	LABEL_OPT="-L rootfs"
fi

# Common to both formats:
# -q        quiet
# -F        don't complain that the target is a plain file
# -b 1024   block size, matching the 2005 image
# -N 1280   inode count, matching the 2005 image
# -m 5      5% reserved, matching the 256-of-5120 blocks the 2005 image had
# -U        a fixed UUID, so two builds of the same tree are identical
# -d        populate from the staging tree
if [ "$FSTYPE" = ext4 ]; then
	mke2fs -q -F \
		-t ext4 \
		$LABEL_OPT \
		-b "$BLOCK_SIZE" \
		-N "$INODE_COUNT" \
		-I 256 \
		-O ^metadata_csum,^64bit,^orphan_file -m 2 \
		-U 13bcf00c-78b2-11d9-8fdf-f213c4292cfb \
		-d "$STAGE" \
		"$OUT" "$BLOCK_COUNT"

	[ -s "$OUT" ] || { echo "mkimage: mke2fs produced nothing" >&2; exit 1; }
else
	mke2fs -q -F \
		$LABEL_OPT \
		-b "$BLOCK_SIZE" \
		-N "$INODE_COUNT" \
		-I 128 \
		-O none -m 5 \
		-U 13bcf00c-78b2-11d9-8fdf-f213c4292cfb \
		-d "$STAGE" \
		"$OUT" "$BLOCK_COUNT" 2>&1 | grep -v '128-byte inodes cannot handle dates'

	[ -s "$OUT" ] || { echo "mkimage: mke2fs produced nothing" >&2; exit 1; }

	debugfs -w -R "ssv rev_level 0" "$OUT" >/dev/null 2>&1 || {
		echo "mkimage: could not demote the filesystem to revision 0" >&2
		exit 1
	}
fi
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

if [ "$MODE" = "bin" ]; then
	python3 port/image/mkverity.py "$OUT" include/linux/verity_roothash.h || exit 1
fi

USB_NTFS_IMG="disk/x86/usb_ntfs.img"
if [ ! -s "$USB_NTFS_IMG" ]; then
	MKNTFS=$(command -v mkntfs || command -v /usr/sbin/mkntfs || command -v /sbin/mkntfs || true)
	NTFSCP=$(command -v ntfscp || command -v /usr/sbin/ntfscp || command -v /sbin/ntfscp || true)
	if [ -n "$MKNTFS" ] && [ -n "$NTFSCP" ]; then
		TMP_README=$(mktemp)
		cat > "$TMP_README" <<'EOF'
=== SanDisk Extreme NTFS USB 3.2 Flash Drive ===
Label:      SANDISK_NTFS
UUID:       6A1B-8E42
Device:     /dev/sda1 (8:1, 2048 KB NTFS)
Mounted by: Android vold -> /bin/ntfs-3g (FUSE /dev/fuse) -> /mnt/media_rw/usb
EOF
		dd if=/dev/zero of="$USB_NTFS_IMG" bs=1024 count=2048 status=none
		"$MKNTFS" -q -F -f -s 512 -c 4096 -p 0 -H 16 -S 63 -L "SANDISK_NTFS" "$USB_NTFS_IMG" >/dev/null 2>&1 || true
		"$NTFSCP" -f "$USB_NTFS_IMG" "$TMP_README" README_USB.txt >/dev/null 2>&1 || true
		rm -f "$TMP_README"
	fi
fi

echo "mkimage: wrote $OUT ($MODE) as $FSTYPE ($(stat -c %s "$OUT") bytes, $present file(s), $missing missing)"
exit 0
