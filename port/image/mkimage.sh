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
export MKE2FS_CONFIG="$SRCROOT/port/image/mke2fs.conf"

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
ext2|ext4|erofs) ;;
*) echo "mkimage: --fstype must be ext2, ext4, or erofs (got '$FSTYPE')" >&2; exit 2 ;;
esac

case "$MODE" in
root|bin|full) ;;
*) echo "mkimage: --mode must be root, bin, or full (got '$MODE')" >&2; exit 2 ;;
esac

UNIFIED_DISK="disk/x86/root"
BUILD_OUT=$(mktemp)
trap 'rm -f "$BUILD_OUT" "$BUILD_OUT.sarthak" "$BUILD_OUT.vendor"' EXIT

STAGE_SYS="port/image/.stage_system"
STAGE_VENDOR="port/image/.stage_vendor"

eval "$(python3 port/image/mksingledisk.py shell-vars)"

if [ "$MODE" = "bin" ]; then
	STAGE="port/image/.stage_bin"
	BLOCK_SIZE=1024
	BLOCK_COUNT=${BLOCK_COUNT:-${SIX_SUBPART_DATA_BLOCKS_1K_SYSTEM_A:-15360}}
	INODE_COUNT=${INODE_COUNT:-512}
	SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1700000000}
	export SOURCE_DATE_EPOCH
else
	# Geometry derived from port/image/disk_layout.json (root partition).
	# Can be overridden via environment variables BLOCK_COUNT and INODE_COUNT.
	BLOCK_SIZE=1024
	BLOCK_COUNT=${BLOCK_COUNT:-${SIX_PART_BLOCKS_1K_ROOT:-51200}}
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
		case "$path" in /bin/*|/system/*|/vendor/*) continue ;; esac
	elif [ "$MODE" = "bin" ]; then
		case "$path" in /bin/*|/system/*|/vendor/*) ;; *) continue ;; esac
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
		*"ELF 32-bit LSB executable"*80386*"statically linked"* | *"ELF 32-bit LSB executable"*i386*"statically linked"* | *"ELF 32-bit LSB relocatable"*80386* | *"ELF 32-bit LSB relocatable"*i386*)
			;;
		*)
			badarch=$((badarch + 1))
			echo "mkimage: WRONG ARCH  $a" >&2
			echo "mkimage:              expected static 32-bit LSB executable/relocatable Intel 80386, got: $desc" >&2
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
if [ "$MODE" = "bin" ]; then
	rm -rf "$STAGE_SYS" "$STAGE_VENDOR"
	mkdir -p "$STAGE_SYS" "$STAGE_VENDOR"
fi

export SRCROOT STAGE STAGE_SYS STAGE_VENDOR MANIFEST OUT BUILD_OUT BLOCK_SIZE BLOCK_COUNT INODE_COUNT FSTYPE MODE

fakeroot -- bash -s <<'FAKEROOT_SCRIPT'
set -u
rc=0

stage_entry() {
	local type="$1" dest="$2" mode="$3" a="${4:-}" b="${5:-}" c="${6:-}"
	case "$type" in
	dir)
		mkdir -p "$dest"           || rc=1
		chmod "$mode" "$dest"      || rc=1
		chown 0:0 "$dest"          || rc=1
		;;
	file)
		[ -f "$a" ] || return 0
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
}

while read -r type path mode a b c; do
	case "$type" in ''|\#*) continue ;; esac

	if [ "$MODE" = "root" ]; then
		case "$path" in
		/bin|/bin/*|/bin-sarthak|/system|/system/*|/vendor|/vendor/*) continue ;;
		esac
		stage_entry "$type" "$STAGE$path" "$mode" "${a:-}" "${b:-}" "${c:-}"
	elif [ "$MODE" = "bin" ]; then
		case "$path" in
		/bin|/bin-sarthak)
			stage_entry "$type" "$STAGE_SYS$path" "$mode" "${a:-}" "${b:-}" "${c:-}"
			;;
		/bin/*)
			stage_entry "$type" "$STAGE${path#/bin}" "$mode" "${a:-}" "${b:-}" "${c:-}"
			stage_entry "$type" "$STAGE_SYS$path" "$mode" "${a:-}" "${b:-}" "${c:-}"
			;;
		/system/*)
			stage_entry "$type" "$STAGE_SYS${path#/system}" "$mode" "${a:-}" "${b:-}" "${c:-}"
			;;
		/vendor/*)
			stage_entry "$type" "$STAGE_VENDOR${path#/vendor}" "$mode" "${a:-}" "${b:-}" "${c:-}"
			;;
		esac
	else
		stage_entry "$type" "$STAGE$path" "$mode" "${a:-}" "${b:-}" "${c:-}"
	fi
done < <(sed 's/#.*//' "$MANIFEST")

chown 0:0 "$STAGE"
chmod 0755 "$STAGE"

if [ "$MODE" = "root" ]; then
	mkdir -p "$STAGE/system" "$STAGE/vendor"
	chmod 0755 "$STAGE/system" "$STAGE/vendor"
	chown 0:0 "$STAGE/system" "$STAGE/vendor"
	ln -sfn /system/bin "$STAGE/bin"
	ln -sfn /system/bin-sarthak "$STAGE/bin-sarthak"
elif [ "$MODE" = "full" ]; then
	mkdir -p "$STAGE/system" "$STAGE/vendor"
	ln -sfn /bin "$STAGE/system/bin"
	ln -sfn /bin-sarthak "$STAGE/system/bin-sarthak"
elif [ "$MODE" = "bin" ]; then
	mkdir -p "$STAGE_SYS/bin-sarthak"
	chmod 0755 "$STAGE_SYS" "$STAGE_SYS/bin-sarthak" "$STAGE_VENDOR"
	chown 0:0 "$STAGE_SYS" "$STAGE_SYS/bin-sarthak" "$STAGE_VENDOR"
	python3 - "$STAGE_SYS/build.prop" "$STAGE_VENDOR/build.prop" << 'PYEOF'
import sys

sys_prop = (
    "# SIX_ANDROID_BUILD_PROP_V1\n"
    "ro.build.Partition=system\n"
    "ro.build.Slot=_a\n"
    "ro.build.id=SIX.261003.001.A1\n"
    "ro.build.version.incremental=20261003.0001\n"
    "ro.build.version.release=16\n"
    "ro.build.version.security_patch=2026-10-01\n"
    "ro.build.fingerprint=google/six_x86/six:16/SIX.261003.001.A1/20261003.0001:user/release-keys\n"
    "ro.product.system.brand=google\n"
    "ro.product.system.name=six_x86\n"
    "ro.product.system.device=six\n"
    "ro.boot.slot_suffix=_a\n"
    "ro.boot.verifiedbootstate=green\n"
    "ro.boot.veritymode=enforcing\n"
    "ro.ota.version=1\n"
).encode("ascii")
sys_prop = sys_prop + b"#" * (1023 - len(sys_prop)) + b"\n"
with open(sys.argv[1], "wb") as f:
    f.write(sys_prop)

vnd_prop = (
    "# SIX_ANDROID_BUILD_PROP_V1\n"
    "ro.build.Partition=vendor\n"
    "ro.build.Slot=_a\n"
    "ro.vendor.build.id=SIX.261003.001.A1\n"
    "ro.vendor.build.version.incremental=20261003.0001\n"
    "ro.vendor.build.version.release=16\n"
    "ro.vendor.build.security_patch=2026-10-01\n"
    "ro.vendor.build.fingerprint=google/six_x86/six:16/SIX.261003.001.A1/20261003.0001:user/release-keys\n"
    "ro.product.vendor.brand=google\n"
    "ro.product.vendor.name=six_x86\n"
    "ro.product.vendor.device=six\n"
    "ro.boot.slot_suffix=_a\n"
    "ro.ota.version=1\n"
).encode("ascii")
vnd_prop = vnd_prop + b"#" * (1023 - len(vnd_prop)) + b"\n"
with open(sys.argv[2], "wb") as f:
    f.write(vnd_prop)
PYEOF
	chmod 0644 "$STAGE_SYS/build.prop" "$STAGE_VENDOR/build.prop"
	chown 0:0 "$STAGE_SYS/build.prop" "$STAGE_VENDOR/build.prop"
fi

if [ "$MODE" != "bin" ]; then
	mkdir -p "$STAGE/etc/fwupd/remotes.d/lvfs/packages"
	if [ -r /google/data/ro/users/mo/motorman/www/data/share/cros/fwupd/ufs/test.cab ]; then
		cp -f /google/data/ro/users/mo/motorman/www/data/share/cros/fwupd/ufs/test.cab \
		      "$STAGE/etc/fwupd/remotes.d/lvfs/packages/samsung-ufs-kludg4uhgc-2101.cab"
	else
		python3 - "$STAGE/etc/fwupd/remotes.d/lvfs/packages/samsung-ufs-kludg4uhgc-2101.cab" << 'PYEOF'
import hashlib, struct, sys

out_path = sys.argv[1]
bin_name = b"SOLVIT_V6_TLC_256Gb_UFS31_GEN1_128GB_P21_FW01_e52a9b8_241106_13h23m.bin"

# Construct 786,432-byte Samsung JEDEC UFS ("UFSH") controller firmware binary
payload = bytearray(786432)
payload[0:4] = b"UFSH"
payload[0x20:0x24] = b"2101"
payload[0x25:0x28] = b"F00"
for i in range(64, len(payload), 4):
    v = (i * 0x45d9f3b) & 0xffffffff
    payload[i:i+4] = struct.pack("<I", v)
payload = bytes(payload)
sha_hex = hashlib.sha256(payload).hexdigest()

xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<!-- Copyright 2024 Google LLC -->
<component type="firmware">
  <id>com.google.KLUDG4UHGC.firmware.2101</id>
  <name>KLUDG4UHGC-B0E1</name>
  <summary>SAMSUNG KLUDG4UHGC-B0E1 Firmware Update</summary>
  <developer_name>Google</developer_name>
  <provides>
    <firmware type="flashed">41a057a2-a1ff-5764-bc6f-71aa045706ff</firmware>
  </provides>
  <custom>
    <value key="LVFS::VersionFormat">plain</value>
    <value key="LVFS::UpdateProtocol">org.jedec.ufs</value>
  </custom>
  <releases>
    <release version="2101" date="2024-10-25" urgency="high">
      <checksum type="sha256" filename="SOLVIT_V6_TLC_256Gb_UFS31_GEN1_128GB_P21_FW01_e52a9b8_241106_13h23m.bin" target="content">{sha_hex}</checksum>
      <description>
        <p>SAMSUNG KLUDG4UHGC-B0E1 UFS Firmware Update</p>
      </description>
    </release>
  </releases>
  <requires>
    <firmware compare="ge" version="0801"/>
  </requires>
</component>
""".encode("utf-8")

readme = b"SAMSUNG KLUDG4UHGC-B0E1 UFS Firmware Update\n"

files = [
    (b"firmware.metainfo.xml", xml),
    (b"README.txt", readme),
    (bin_name, payload),
]

folder_stream = b"".join(data for _, data in files)
blocks = [folder_stream[off:off + 32768] for off in range(0, len(folder_stream), 32768)]

coff_files = 36 + 8
cffiles = bytearray()
uoff = 0
for name, data in files:
    cffiles += struct.pack("<IIHHHH", len(data), uoff, 0, 0x5959, 0x0000, 0x20) + name + b"\x00"
    uoff += len(data)

coff_data = coff_files + len(cffiles)
cfdata = bytearray()
for chunk in blocks:
    cfdata += struct.pack("<IHH", 0, len(chunk), len(chunk)) + chunk

total_cab = coff_data + len(cfdata)
chdr = struct.pack("<IIIIIIBBHHHHH",
                   0x4643534D, 0, total_cab, 0, coff_files, 0,
                   3, 1, 1, len(files), 0, 0, 0)
cfolder = struct.pack("<IHH", coff_data, len(blocks), 0)

with open(out_path, "wb") as f:
    f.write(chdr + cfolder + cffiles + cfdata)
PYEOF
	fi
	chmod 0644 "$STAGE/etc/fwupd/remotes.d/lvfs/packages/samsung-ufs-kludg4uhgc-2101.cab"
	chown 0:0 "$STAGE/etc/fwupd/remotes.d/lvfs/packages/samsung-ufs-kludg4uhgc-2101.cab"

	python3 - "$STAGE/etc/fwupd/remotes.d/lvfs/packages/wd-nvme-sn850x-624711WD.cab" << 'PYEOF'
import hashlib, struct, sys, zlib

out_path = sys.argv[1]
# 40,960-byte opaque/encrypted Western Digital NVMe controller microcode image
# (no SFWM/UFSH magic and no ASCII version string inside the binary payload)
payload = bytes(((i * 73 + 0xA5) & 0xFF) for i in range(40960))
sha_hex = hashlib.sha256(payload).hexdigest()

xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<component type="firmware">
  <id>com.wdc.SN850X.firmware</id>
  <name>WD_BLACK SN850X NVMe SSD</name>
  <summary>Western Digital NVMe SSD Controller Firmware</summary>
  <developer_name>Western Digital Technologies, Inc.</developer_name>
  <provides>
    <firmware type="flashed">c89408b5-6375-51c2-9b85-8f01e1a9d411</firmware>
  </provides>
  <custom>
    <value key="LVFS::VersionFormat">plain</value>
    <value key="LVFS::UpdateProtocol">org.nvmexpress</value>
    <value key="LVFS::Plugin">nvme</value>
  </custom>
  <releases>
    <release version="624711WD" date="2026-09-28" urgency="high">
      <checksum type="sha256" filename="624711WD.fluf" target="content">{sha_hex}</checksum>
    </release>
  </releases>
</component>
""".encode("utf-8")

files = [
    (b"firmware.metainfo.xml", xml),
    (b"624711WD.fluf", payload),
]

folder_stream = b"".join(data for _, data in files)
# Split into 32,768-byte CFDATA blocks and compress each with MSZIP ("CK" + raw DEFLATE)
blocks = []
for off in range(0, len(folder_stream), 32768):
    u_chunk = folder_stream[off:off + 32768]
    c_obj = zlib.compressobj(6, zlib.DEFLATED, -15)
    c_chunk = b"CK" + c_obj.compress(u_chunk) + c_obj.flush()
    blocks.append((u_chunk, c_chunk))

coff_files = 36 + 8
cffiles = bytearray()
uoff = 0
for name, data in files:
    cffiles += struct.pack("<IIHHHH", len(data), uoff, 0, 0x5d3c, 0x0800, 0x20) + name + b"\x00"
    uoff += len(data)

coff_data = coff_files + len(cffiles)
cfdata = bytearray()
for u_chunk, c_chunk in blocks:
    cfdata += struct.pack("<IHH", 0, len(c_chunk), len(u_chunk)) + c_chunk

total_cab = coff_data + len(cfdata)
chdr = struct.pack("<IIIIIIBBHHHHH",
                   0x4643534D, 0, total_cab, 0, coff_files, 0,
                   3, 1, 1, len(files), 0, 0x5744, 0)
cfolder = struct.pack("<IHH", coff_data, len(blocks), 1)  # typeCompress = 1 (MSZIP)

with open(out_path, "wb") as f:
    f.write(chdr + cfolder + cffiles + cfdata)
PYEOF
	chmod 0644 "$STAGE/etc/fwupd/remotes.d/lvfs/packages/wd-nvme-sn850x-624711WD.cab"
	chown 0:0 "$STAGE/etc/fwupd/remotes.d/lvfs/packages/wd-nvme-sn850x-624711WD.cab"
fi

if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
	find "$STAGE" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} + 2>/dev/null || true
	if [ "$MODE" = "bin" ]; then
		find "$STAGE_SYS" "$STAGE_VENDOR" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} + 2>/dev/null || true
	fi
fi

[ "$rc" = 0 ] || { echo "mkimage: staging failed" >&2; exit 1; }

rm -f "$BUILD_OUT"

LABEL_OPT=""
PRIMARY_STAGE="$STAGE"
EROFS_MODE="$MODE"
if [ "$MODE" = "bin" ]; then
	LABEL_OPT="-L bin_verity"
	PRIMARY_STAGE="$STAGE_SYS"
	EROFS_MODE="system"
else
	LABEL_OPT="-L rootfs"
fi

EXT4_FEATURES="none,has_journal,extent,huge_file,flex_bg,dir_nlink,extra_isize,ext_attr,resize_inode,dir_index,filetype,sparse_super,large_file"

if [ "$FSTYPE" = erofs ]; then
	LABEL_NAME="rootfs"
	[ "$MODE" = "bin" ] && LABEL_NAME="bin_verity"
	python3 port/image/mkerofs.py \
		--stage "$PRIMARY_STAGE" \
		--manifest "$MANIFEST" \
		--mode "$EROFS_MODE" \
		--label "$LABEL_NAME" \
		--uuid 13bcf00c-78b2-11d9-8fdf-f213c4292cfb \
		--blocks "$BLOCK_COUNT" \
		--out "$BUILD_OUT" || exit 1

	[ -s "$BUILD_OUT" ] || { echo "mkimage: mkerofs.py produced nothing" >&2; exit 1; }
elif [ "$FSTYPE" = ext4 ]; then
	mke2fs -q -F \
		-t ext4 \
		$LABEL_OPT \
		-b "$BLOCK_SIZE" \
		-N "$INODE_COUNT" \
		-I 256 \
		-O "$EXT4_FEATURES" -m 2 \
		-U 13bcf00c-78b2-11d9-8fdf-f213c4292cfb \
		-d "$PRIMARY_STAGE" \
		"$BUILD_OUT" "$BLOCK_COUNT"

	[ -s "$BUILD_OUT" ] || { echo "mkimage: mke2fs produced nothing" >&2; exit 1; }
else
	mke2fs -q -F \
		$LABEL_OPT \
		-b "$BLOCK_SIZE" \
		-N "$INODE_COUNT" \
		-I 128 \
		-O none -m 5 \
		-U 13bcf00c-78b2-11d9-8fdf-f213c4292cfb \
		-d "$PRIMARY_STAGE" \
		"$BUILD_OUT" "$BLOCK_COUNT" 2>&1 | grep -v '128-byte inodes cannot handle dates'

	[ -s "$BUILD_OUT" ] || { echo "mkimage: mke2fs produced nothing" >&2; exit 1; }

	debugfs -w -R "ssv rev_level 0" "$BUILD_OUT" >/dev/null 2>&1 || {
		echo "mkimage: could not demote the filesystem to revision 0" >&2
		exit 1
	}
fi

if [ "$MODE" = "bin" ]; then
	python3 port/image/mkerofs.py \
		--stage "$STAGE" \
		--manifest "$MANIFEST" \
		--mode "bin" \
		--label "bin_sarthak" \
		--uuid 13bcf00c-78b2-11d9-8fdf-f213c4292cfd \
		--blocks "$BLOCK_COUNT" \
		--verity \
		--out "$BUILD_OUT.sarthak" || exit 1

	python3 port/image/mkerofs.py \
		--stage "$STAGE_VENDOR" \
		--manifest "$MANIFEST" \
		--mode "vendor" \
		--label "vendor_verity" \
		--uuid 13bcf00c-78b2-11d9-8fdf-f213c4292cfe \
		--blocks 3072 \
		--out "$BUILD_OUT.vendor" || exit 1
fi
FAKEROOT_SCRIPT

rc=$?
[ "$rc" = 0 ] || { echo "mkimage: failed" >&2; exit "$rc"; }

if [ "$FSTYPE" != erofs ]; then
	if command -v tune2fs >/dev/null 2>&1; then
		tune2fs -c 0 -i 0 "$BUILD_OUT" >/dev/null 2>&1
	fi

	if [ -n "${SOURCE_DATE_EPOCH:-}" ] && command -v debugfs >/dev/null 2>&1; then
		debugfs -w -R "ssv mtime @$SOURCE_DATE_EPOCH" "$BUILD_OUT" >/dev/null 2>&1
		debugfs -w -R "ssv wtime @$SOURCE_DATE_EPOCH" "$BUILD_OUT" >/dev/null 2>&1
		debugfs -w -R "ssv lastcheck @$SOURCE_DATE_EPOCH" "$BUILD_OUT" >/dev/null 2>&1
	fi
fi

if [ "$MODE" = "bin" ]; then
	python3 port/image/mkverity.py "$BUILD_OUT" include/linux/verity_roothash.h || exit 1
	mkdir -p port/image
	cp -f "$BUILD_OUT" port/image/.bin_part.img
	python3 port/image/mkotazip.py \
		--disk port/image/.bin_part.img \
		--build-id SIX.261004.002.B2 \
		--slot _b \
		--ota-version 2 \
		--rollback-index 2 \
		-o port/image/ota.zip >/dev/null 2>&1 || true
	if [ -f "$UNIFIED_DISK" ]; then
		python3 port/image/mksingledisk.py write-part "$UNIFIED_DISK" bin_storage "$BUILD_OUT" || exit 1
	fi
	if [ "$OUT" != "disk/x86/root" ] && [ "$OUT" != "disk/x86/bin_storage" ]; then
		cp -f "$BUILD_OUT" "$OUT"
	fi
	rm -f disk/x86/bin_storage
	touch -d "+1 second" port/image/.bin_stamp 2>/dev/null || touch port/image/.bin_stamp
	echo "mkimage: staged $UNIFIED_DISK [p4:bin_storage] ($MODE) as $FSTYPE ($(stat -c %s port/image/.bin_part.img) bytes, $present file(s), $missing missing)"
	exit 0
fi

# Mode is "root" or "full": write Partition 1 (root) and Partition 4 (bin_storage) into $OUT
if [ "$MODE" = "full" ]; then
	python3 port/image/mksingledisk.py zero-part "$OUT" bin_storage || exit 1
elif [ -s port/image/.bin_part.img ]; then
	python3 port/image/mksingledisk.py write-part "$OUT" bin_storage port/image/.bin_part.img || exit 1
fi
python3 port/image/mksingledisk.py write-part "$OUT" root "$BUILD_OUT" || exit 1

if [ "$MODE" = "root" ] || ! python3 port/image/mksingledisk.py has-part "$OUT" usb_ext2; then
	USB_STAGE=$(mktemp -d)
	USB_TMP=$(mktemp)
	mkdir -p "$USB_STAGE/Android/data" "$USB_STAGE/DCIM" "$USB_STAGE/Documents" \
		"$USB_STAGE/Download" "$USB_STAGE/Movies" "$USB_STAGE/Music" "$USB_STAGE/Pictures"
	cat > "$USB_STAGE/README_USB.txt" <<'EOF'
=== SanDisk Ultra USB 3.0 Flash Drive (ext2) ===
Label:      SAN_DISK_USB
UUID:       4A8F-9C21
Device:     /dev/sda1 (8:1, 2048 KB ext2, host image disk/x86/root [p7:usb_ext2])
Mounted by: Android vold -> kernel ext2 -> /mnt/media_rw/4A8F-9C21 -> /storage/4A8F-9C21
EOF
	echo "Camera DCIM sample photo metadata (SanDisk ext2 USB)" > "$USB_STAGE/DCIM/IMG_0001.TXT"
	fakeroot -- mke2fs -q -F -b 1024 -N 256 -I 128 -O none -m 0 \
		-L "SAN_DISK_USB" -d "$USB_STAGE" "$USB_TMP" "${SIX_PART_BLOCKS_1K_USB_EXT2:-2048}" 2>/dev/null || true
	debugfs -w -R "ssv rev_level 0" "$USB_TMP" >/dev/null 2>&1 || true
	python3 port/image/mksingledisk.py write-part "$OUT" usb_ext2 "$USB_TMP" || exit 1
	rm -rf "$USB_STAGE" "$USB_TMP"
fi

if [ "$MODE" = "root" ] || ! python3 port/image/mksingledisk.py has-part "$OUT" usb_ext4; then
	USB_STAGE=$(mktemp -d)
	USB_TMP=$(mktemp)
	mkdir -p "$USB_STAGE/Android/data" "$USB_STAGE/DCIM" "$USB_STAGE/Documents" \
		"$USB_STAGE/Download" "$USB_STAGE/Movies" "$USB_STAGE/Music" "$USB_STAGE/Pictures"
	cat > "$USB_STAGE/README_USB.txt" <<'EOF'
=== SanDisk Extreme PRO USB 3.1 Flash Drive (ext4) ===
Label:      SANDISK_EXT4
UUID:       5B9E-7D31
Device:     /dev/sda1 (8:1, 2048 KB ext4 with extents, host image disk/x86/root [p8:usb_ext4])
Mounted by: Android vold -> kernel ext4 -> /mnt/media_rw/7B9E-3D10 -> /storage/7B9E-3D10
EOF
	echo "Camera DCIM sample photo metadata (SanDisk ext4 USB)" > "$USB_STAGE/DCIM/IMG_0001.TXT"
	fakeroot -- mke2fs -q -F -t ext4 -b 1024 -N 256 -I 256 \
		-O "none,has_journal,extent,huge_file,flex_bg,dir_nlink,extra_isize,ext_attr,resize_inode,dir_index,filetype,sparse_super,large_file" -m 0 \
		-L "SANDISK_EXT4" -U "7b9e3d10-0000-4000-8000-000000000001" -d "$USB_STAGE" "$USB_TMP" "${SIX_PART_BLOCKS_1K_USB_EXT4:-2048}" 2>/dev/null || true
	python3 port/image/mksingledisk.py write-part "$OUT" usb_ext4 "$USB_TMP" || exit 1
	rm -rf "$USB_STAGE" "$USB_TMP"
fi

if [ "$MODE" = "root" ] || ! python3 port/image/mksingledisk.py has-part "$OUT" usb_erofs; then
	USB_STAGE=$(mktemp -d)
	USB_TMP=$(mktemp)
	mkdir -p "$USB_STAGE/Android/data" "$USB_STAGE/DCIM" "$USB_STAGE/Documents" \
		"$USB_STAGE/Download" "$USB_STAGE/Movies" "$USB_STAGE/Music" "$USB_STAGE/Pictures"
	cat > "$USB_STAGE/README_USB.txt" <<'EOF'
=== SanDisk Extreme EROFS Read-Only Flash Drive ===
Label:      SANDISK_EROFS
UUID:       E0F5-2026
Device:     /dev/sda1 (8:1, 2048 KB EROFS v1, host image disk/x86/root [p9:usb_erofs])
Mounted by: Android vold -> kernel erofs (ro) -> /mnt/media_rw/E0F5-2026 -> /storage/E0F5-2026
EOF
	echo "Camera DCIM sample photo metadata (SanDisk EROFS USB)" > "$USB_STAGE/DCIM/IMG_0001.TXT"
	python3 port/image/mkerofs.py \
		--stage "$USB_STAGE" \
		--label "SANDISK_EROFS" \
		--uuid 7e0f5e1e-0000-4000-8000-000000000001 \
		--blocks "${SIX_PART_BLOCKS_1K_USB_EROFS:-2048}" \
		--out "$USB_TMP" 2>/dev/null || true
	python3 port/image/mksingledisk.py write-part "$OUT" usb_erofs "$USB_TMP" || exit 1
	rm -rf "$USB_STAGE" "$USB_TMP"
fi

if [ "$MODE" = "root" ] || ! python3 port/image/mksingledisk.py has-part "$OUT" usb_ntfs; then
	MKNTFS=$(command -v mkntfs || command -v /usr/sbin/mkntfs || command -v /sbin/mkntfs || true)
	NTFSCP=$(command -v ntfscp || command -v /usr/sbin/ntfscp || command -v /sbin/ntfscp || true)
	if [ -n "$MKNTFS" ] && [ -n "$NTFSCP" ]; then
		TMP_README=$(mktemp)
		USB_TMP=$(mktemp)
		cat > "$TMP_README" <<'EOF'
=== SanDisk Extreme NTFS USB 3.2 Flash Drive ===
Label:      SANDISK_NTFS
UUID:       6A1B-8E42
Device:     /dev/sda1 (8:1, 2048 KB NTFS, host image disk/x86/root [p10:usb_ntfs])
Mounted by: Android vold -> /bin/ntfs-3g (FUSE /dev/fuse) -> /mnt/media_rw/6A1B-8E42 -> /storage/6A1B-8E42
EOF
		dd if=/dev/zero of="$USB_TMP" bs=1024 count="${SIX_PART_BLOCKS_1K_USB_NTFS:-2048}" status=none
		"$MKNTFS" -q -F -f -s 512 -c 4096 -p 0 -H 16 -S 63 -L "SANDISK_NTFS" "$USB_TMP" >/dev/null 2>&1 || true
		"$NTFSCP" -f "$USB_TMP" "$TMP_README" README_USB.txt >/dev/null 2>&1 || true
		python3 port/image/mksingledisk.py write-part "$OUT" usb_ntfs "$USB_TMP" || exit 1
		rm -f "$TMP_README" "$USB_TMP"
	fi
fi

if [ "$MODE" = "root" ] || ! python3 port/image/mksingledisk.py has-part "$OUT" nvme0n1; then
	NVME_STAGE=$(mktemp -d)
	NVME_TMP=$(mktemp)
	mkdir -p "$NVME_STAGE/media/0/Android/data" \
		"$NVME_STAGE/media/0/DCIM" \
		"$NVME_STAGE/media/0/Documents" \
		"$NVME_STAGE/media/0/Download" \
		"$NVME_STAGE/media/0/Movies" \
		"$NVME_STAGE/media/0/Music" \
		"$NVME_STAGE/media/0/Pictures"
	fakeroot -- mke2fs -q -F -t ext4 -b 1024 -N 2048 -I 256 \
		-O "none,has_journal,extent,huge_file,flex_bg,dir_nlink,extra_isize,ext_attr,resize_inode,dir_index,filetype,sparse_super,large_file" -m 0 \
		-L "userdata" -U "a1b2c3d4-2026-4000-8000-000000000001" -d "$NVME_STAGE" "$NVME_TMP" "${SIX_PART_BLOCKS_1K_NVME:-16384}" 2>/dev/null || true
	if command -v tune2fs >/dev/null 2>&1; then
		tune2fs -c 0 -i 0 "$NVME_TMP" >/dev/null 2>&1 || true
	fi
	python3 port/image/mksingledisk.py write-part "$OUT" nvme0n1 "$NVME_TMP" || exit 1
	rm -rf "$NVME_STAGE" "$NVME_TMP"
fi

if [ "$MODE" = "root" ] || ! python3 port/image/mksingledisk.py has-part "$OUT" ufs0; then
	UFS_STAGE=$(mktemp -d)
	UFS_LUN0_TMP=$(mktemp)
	UFS_TMP=$(mktemp)
	mkdir -p "$UFS_STAGE/ota"
	cat > "$UFS_STAGE/UFS.TXT" <<'EOF'
=== SIX JEDEC UFS 4.0 Multi-LUN Storage (/dev/ufsa -> /ufs) ===
Controller: /dev/ufs-bsg0 (UFSHCI 4.0, MIPI UniPro HS-Gear5 2-Lane)
Host Image: ./disk/x86/root [p6:ufs0] (5 MB unified UFS flash package)
  - LUN 0 (/dev/ufsa, 58:0): 4096 KB ext4 volume mounted at /ufs
  - LUN 1 (/dev/ufsb, 58:1):  384 KB Boot LUN A (slot_a primary bootloader)
  - LUN 2 (/dev/ufsc, 58:2):  384 KB Boot LUN B (slot_b secondary OTA slot)
  - W-LUN (/dev/ufs-rpmb):    128 KB Replay Protected Memory Block (HMAC-SHA256)
EOF
	echo "active_slot=a" > "$UFS_STAGE/ota/slot_status.txt"
	if [ -s port/image/.bin_part.img ]; then
		python3 port/image/mkotazip.py \
			--disk port/image/.bin_part.img \
			--build-id SIX.261004.002.B2 \
			--slot _b \
			--ota-version 2 \
			--rollback-index 2 \
			-o "$UFS_STAGE/ota/ota.zip" >/dev/null 2>&1 || true
		cp -f "$UFS_STAGE/ota/ota.zip" port/image/ota.zip 2>/dev/null || true
	fi
	fakeroot -- mke2fs -q -F -t ext4 -b 1024 -N 512 -I 256 \
		-O "none,has_journal,extent,huge_file,flex_bg,dir_nlink,extra_isize,ext_attr,resize_inode,dir_index,filetype,sparse_super,large_file" -m 0 \
		-L "ufs_data" -U "c0ffee40-2026-4000-8000-000000000001" -d "$UFS_STAGE" "$UFS_LUN0_TMP" $(( (${SIX_PART_BLOCKS_1K_UFS:-5120}) - 1024 )) 2>/dev/null || true
	if command -v tune2fs >/dev/null 2>&1; then
		tune2fs -c 0 -i 0 "$UFS_LUN0_TMP" >/dev/null 2>&1 || true
	fi
	truncate -s "${SIX_PART_SIZE_BYTES_UFS:-5242880}" "$UFS_TMP"
	dd if="$UFS_LUN0_TMP" of="$UFS_TMP" bs=1024 seek=1024 conv=notrunc status=none 2>/dev/null || true
	python3 port/image/mksingledisk.py write-part "$OUT" ufs0 "$UFS_TMP" || exit 1
	rm -rf "$UFS_STAGE" "$UFS_LUN0_TMP" "$UFS_TMP"
fi

if ! python3 port/image/mksingledisk.py has-part "$OUT" aux_storage-1; then
	bash port/image/mkaux.sh --force --out "$OUT" || exit 1
fi

# Clean up any legacy separate image files so disk/x86/ contains only root
rm -f disk/x86/bin_storage disk/x86/aux_storage-1 disk/x86/aux_storage-2 \
      disk/x86/nvme0n1.img disk/x86/ufs0.img \
      disk/x86/usb_ext2.img disk/x86/usb_ext4.img disk/x86/usb_erofs.img \
      disk/x86/usb_ntfs.img disk/x86/usb_crypt.img

echo "mkimage: wrote $OUT [${SIX_DISK_NUM_PARTITIONS:-11} GPT partitions from disk_layout.json] ($MODE) as $FSTYPE ($(stat -c %s "$OUT") bytes, $present file(s), $missing missing)"
exit 0
