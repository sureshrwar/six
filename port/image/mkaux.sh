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

case "$(uname -m)" in
x86_64|i?86) ARCH_DIR=disk/x86 ;;
sparc*)      ARCH_DIR=disk/sparc ;;
*)           ARCH_DIR=disk/x86 ;;
esac

OUT="$ARCH_DIR/aux_storage-1"
FSTYPE="${SIX_AUX_FSTYPE:-ext4}"
FORCE=0
LABEL="six-aux-1"

usage() {
	cat <<EOF
Usage: mkaux.sh [--fstype ext2|ext4] [--out PATH] [--force]

Creates the optional auxiliary disk that SIX exposes as /dev/hdb.  Nothing
mounts it; that is up to /etc/rc or whoever is at the shell:

    mkdir -p /aux/storage-1
    mount /dev/hdb /aux/storage-1

Options:
  --fstype ext2|ext4   on-disk format (default: \$SIX_AUX_FSTYPE, else ext4)
  --out PATH           where to write it (default: $ARCH_DIR/aux_storage-1)
  --force              overwrite an existing image instead of refusing
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
ext2|ext4) ;;
*) echo "mkaux: --fstype must be ext2 or ext4 (got '$FSTYPE')" >&2; exit 2 ;;
esac

# Same geometry as the root disk: 50 MB in 1 KB blocks.  There is no reason
# they have to match, but there is no reason for them to differ either, and
# a shared number is one less thing to explain.
BLOCK_SIZE=1024
BLOCK_COUNT=${BLOCK_COUNT:-51200}
INODE_COUNT=${INODE_COUNT:-$((BLOCK_COUNT / 4))}

for tool in fakeroot mke2fs; do
	command -v "$tool" >/dev/null 2>&1 || {
		echo "mkaux: $tool not found -- install it (Debian: e2fsprogs, fakeroot)" >&2
		exit 1
	}
done

# Refuse by default.  Unlike the root disk, this one is expected to hold
# things the user put there, and it is not reconstructible from the tree --
# there is no manifest to rebuild it from.  Silently truncating it because
# somebody typed "make aux-image" twice would be unkind.
if [ -e "$OUT" ] && [ "$FORCE" -eq 0 ]; then
	echo "mkaux: $OUT already exists; use --force to overwrite it" >&2
	exit 1
fi

mkdir -p "$(dirname "$OUT")"

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

cat > "$STAGE/README" <<EOF
This is the SIX auxiliary disk, /dev/hdb.

It is a $FSTYPE filesystem in a flat file, $ARCH_DIR/aux_storage-1 on the
host, reached through the emulated IDE controller exactly as the root disk
is.  Nothing mounts it for you.  To attach it:

    mkdir -p /aux/storage-1
    mount /dev/hdb /aux/storage-1

and before halting:

    umount /aux/storage-1

The umount matters.  halt(8) does not sync and sys_reboot() does not
either, so without it the superblock stays marked dirty and e2fsck on the
host reports the filesystem as not cleanly unmounted.

If you are reading this file, the mount worked.
EOF

rm -f "$OUT"

if [ "$FSTYPE" = "ext4" ]; then
	# The same three features have to be off as for the root image, for
	# the same reasons -- see the long comment in mkimage.sh:
	#   ^metadata_csum  SIX has no crc32c, so it can neither verify nor
	#                   maintain the checksums it would be required to.
	#   ^64bit          64-byte group descriptors; SIX is strictly 32-bit
	#                   and uses the 32-byte layout.
	#   ^orphan_file    an unknown INCOMPAT bit makes ext4_read_super()
	#                   refuse the mount.
	fakeroot -- mke2fs -q -F \
		-t ext4 \
		-b "$BLOCK_SIZE" \
		-N "$INODE_COUNT" \
		-I 256 \
		-O ^metadata_csum,^64bit,^orphan_file -m 0 \
		-L "$LABEL" \
		-d "$STAGE" \
		"$OUT" "$BLOCK_COUNT"
else
	# Revision-0, feature-free ext2, demoted after the fact: mke2fs 1.47
	# has no way to ask for revision 0 directly.  Again, see mkimage.sh.
	fakeroot -- mke2fs -q -F \
		-b "$BLOCK_SIZE" \
		-N "$INODE_COUNT" \
		-I 128 \
		-O none -m 0 \
		-L "$LABEL" \
		-d "$STAGE" \
		"$OUT" "$BLOCK_COUNT" 2>&1 | grep -v '128-byte inodes cannot handle dates' || true

	[ -s "$OUT" ] || { echo "mkaux: mke2fs produced nothing" >&2; exit 1; }

	debugfs -w -R "ssv rev_level 0" "$OUT" >/dev/null 2>&1 || {
		echo "mkaux: could not demote the filesystem to revision 0" >&2
		exit 1
	}
fi

[ -s "$OUT" ] || { echo "mkaux: mke2fs produced nothing" >&2; exit 1; }

# -m 0 above: no reserved blocks.  The 5% root reservation on the root disk
# exists so the system can still be repaired when userland fills the disk.
# Nothing is ever repaired from this one, so the space is better given back.
e2fsck -fn "$OUT" >/dev/null 2>&1 || {
	echo "mkaux: e2fsck is unhappy with the image just created" >&2
	e2fsck -fn "$OUT" >&2 || true
	exit 1
}

size_mb=$(( BLOCK_COUNT * BLOCK_SIZE / 1024 / 1024 ))
echo "mkaux: wrote $OUT ($FSTYPE, ${size_mb} MB)"
echo "mkaux: it is NOT mounted at boot -- see /README on the disk, or run"
echo "mkaux:     mkdir -p /aux/storage-1 && mount /dev/hdb /aux/storage-1"
