#!/bin/bash
#
# mkaux.sh - build the optional auxiliary disk for SIX (/dev/hdb)
#
# ---------------------------------------------------------------------------
# What this is, and why it is separate from mkimage.sh
#
# The root disk is mandatory and its contents are dictated by a manifest:
# it has to hold a specific set of binaries at specific paths, because
# init(8) is going to look for them.  The auxiliary disk is the opposite of
# all of that.  It is optional, its contents are the user's business, and
# nothing in SIX knows or cares what is on it.  So it gets its own script
# rather than a mode flag on mkimage.sh, and it is deliberately NOT wired
# into the default "make" -- an absent aux disk is the normal state, and
# that path has to keep working.
#
# The only thing seeded onto the filesystem is a README, purely so that a
# successful mount looks different from a silent failure.  "ls /aux" that
# prints nothing is ambiguous; "ls /aux" that prints README is not.
#
# ---------------------------------------------------------------------------
# Why ext4 by default, and why ext2 is worth having
#
# The root filesystem's type is whatever port/image/.fstype says, and the
# aux disk's type is independent of it.  That combination is the point:
# mounting an ext2 /dev/hdb under an ext4 root drives get_fs_type() down
# both branches in a single boot and proves the two drivers coexist on
# different devices, which mounting the root filesystem alone can never
# show.  Choose with:
#
#     make aux-image                       # ext4 (default)
#     make aux-image SIX_AUX_FSTYPE=ext2   # ext2
#
# fakeroot is used for the same reason mkimage.sh uses it: mke2fs -d has to
# be able to chown the seeded files to root without anyone becoming root.
#
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
cd "$top"
export MKE2FS_CONFIG="$here/mke2fs.conf"

case "$(uname -m)" in
x86_64|i?86) ARCH_DIR=disk/x86 ;;
sparc*)      ARCH_DIR=disk/sparc ;;
*)           ARCH_DIR=disk/x86 ;;
esac

OUT="$ARCH_DIR/root"
FSTYPE="${SIX_AUX_FSTYPE:-ntfs}"
FORCE=0
LABEL="six-aux-1"

usage() {
	cat <<EOF
Usage: mkaux.sh [--fstype ext2|ext4|ntfs] [--out PATH] [--force]

Creates the auxiliary disk that SIX exposes as /dev/hdb.  At boot, /etc/rc
invokes /bin/mount_all to auto-detect the filesystem and mount it on
/aux/storage-1.

Options:
  --fstype ext2|ext4|ntfs  on-disk format (default: \$SIX_AUX_FSTYPE, else ntfs)
  --out PATH               where to write it (default: $ARCH_DIR/root [p2:aux_storage-1])
  --force                  overwrite an existing image instead of refusing
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
	--fstype) FSTYPE="${2:-}"; shift ;;
	--out)    OUT="${2:-}"; shift ;;
	--force)  FORCE=1 ;;
	-h|--help) usage; exit 0 ;;
	*) echo "mkaux: unknown argument '$1'" >&2; usage >&2; exit 2 ;;
	esac
	shift
done

case "$FSTYPE" in
ext2|ext4|ntfs) ;;
*) echo "mkaux: --fstype must be ext2, ext4, or ntfs (got '$FSTYPE')" >&2; exit 2 ;;
esac

BLOCK_SIZE=1024
BLOCK_COUNT=${BLOCK_COUNT:-51200}
INODE_COUNT=${INODE_COUNT:-$((BLOCK_COUNT / 4))}

if [ "$FSTYPE" = "ntfs" ]; then
	MKNTFS=$(command -v mkntfs || command -v /usr/sbin/mkntfs || command -v /sbin/mkntfs || true)
	NTFSCP=$(command -v ntfscp || command -v /usr/sbin/ntfscp || command -v /sbin/ntfscp || true)
	if [ -z "$MKNTFS" ] || [ -z "$NTFSCP" ]; then
		echo "mkaux: mkntfs/ntfscp not found -- install ntfs-3g" >&2
		exit 1
	fi
else
	for tool in fakeroot mke2fs; do
		command -v "$tool" >/dev/null 2>&1 || {
			echo "mkaux: $tool not found -- install it (Debian: e2fsprogs, fakeroot)" >&2
			exit 1
		}
	done
fi

USE_UNIFIED=0
if [ "$OUT" = "$ARCH_DIR/root" ] || [ "$OUT" = "$ARCH_DIR/aux_storage-1" ]; then
	USE_UNIFIED=1
	OUT="$ARCH_DIR/root"
fi

if [ "$USE_UNIFIED" -eq 0 ] && [ -e "$OUT" ] && [ "$FORCE" -eq 0 ]; then
	echo "mkaux: $OUT already exists; use --force to overwrite it" >&2
	exit 1
fi

mkdir -p "$(dirname "$OUT")"

STAGE=$(mktemp -d)
BUILD_AUX=$(mktemp)
trap 'rm -rf "$STAGE" "$BUILD_AUX"' EXIT

cat > "$STAGE/README" <<EOF
This is the SIX auxiliary disk, /dev/hdb.

It is a $FSTYPE filesystem in Partition 2 (aux_storage-1) of $ARCH_DIR/root
on the host, reached through the emulated IDE controller exactly as the root
disk is.  At boot, /etc/rc runs /bin/mount_all, which detects the filesystem
type on /dev/hdb (NTFS, ext4, or ext2) and mounts it on /aux/storage-1.

When shutting down with halt(8), /aux/storage-1 is cleanly unmounted and
synced before reboot().

If you are reading this file, the mount worked.
EOF

rm -f "$BUILD_AUX"

if [ "$FSTYPE" = "ntfs" ]; then
	dd if=/dev/zero of="$BUILD_AUX" bs="$BLOCK_SIZE" count="$BLOCK_COUNT" status=none
	"$MKNTFS" -q -F -s 512 -p 0 -H 16 -S 63 -L "$LABEL" "$BUILD_AUX" 2>&1 | \
		grep -v 'is not a block device\|mkntfs forced anyway' || true
	[ -s "$BUILD_AUX" ] || { echo "mkaux: mkntfs produced nothing" >&2; exit 1; }
	"$NTFSCP" -f "$BUILD_AUX" "$STAGE/README" README
elif [ "$FSTYPE" = "ext4" ]; then
	fakeroot -- mke2fs -q -F \
		-t ext4 \
		-b "$BLOCK_SIZE" \
		-N "$INODE_COUNT" \
		-I 256 \
		-O none,has_journal,extent,huge_file,flex_bg,dir_nlink,extra_isize,ext_attr,resize_inode,dir_index,filetype,sparse_super,large_file -m 0 \
		-L "$LABEL" \
		-d "$STAGE" \
		"$BUILD_AUX" "$BLOCK_COUNT"
else
	fakeroot -- mke2fs -q -F \
		-b "$BLOCK_SIZE" \
		-N "$INODE_COUNT" \
		-I 128 \
		-O none -m 0 \
		-L "$LABEL" \
		-d "$STAGE" \
		"$BUILD_AUX" "$BLOCK_COUNT" 2>&1 | grep -v '128-byte inodes cannot handle dates' || true

	[ -s "$BUILD_AUX" ] || { echo "mkaux: mke2fs produced nothing" >&2; exit 1; }

	debugfs -w -R "ssv rev_level 0" "$BUILD_AUX" >/dev/null 2>&1 || {
		echo "mkaux: could not demote the filesystem to revision 0" >&2
		exit 1
	}
fi

[ -s "$BUILD_AUX" ] || { echo "mkaux: produced nothing" >&2; exit 1; }

if [ "$FSTYPE" != "ntfs" ]; then
	e2fsck -fn "$BUILD_AUX" >/dev/null 2>&1 || {
		echo "mkaux: e2fsck is unhappy with the image just created" >&2
		e2fsck -fn "$BUILD_AUX" >&2 || true
		exit 1
	}
fi

if [ "$USE_UNIFIED" -eq 1 ]; then
	python3 port/image/mksingledisk.py write-part "$OUT" aux_storage-1 "$BUILD_AUX" || exit 1
	rm -f "$ARCH_DIR/aux_storage-1"
	size_mb=$(( BLOCK_COUNT * BLOCK_SIZE / 1024 / 1024 ))
	echo "mkaux: wrote $OUT [p2:aux_storage-1] ($FSTYPE, ${size_mb} MB)"
else
	cp -f "$BUILD_AUX" "$OUT"
	size_mb=$(( BLOCK_COUNT * BLOCK_SIZE / 1024 / 1024 ))
	echo "mkaux: wrote $OUT ($FSTYPE, ${size_mb} MB)"
fi
echo "mkaux: /etc/rc will auto-detect $FSTYPE via /bin/mount_all and mount it on /aux/storage-1"
