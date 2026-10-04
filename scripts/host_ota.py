#!/usr/bin/env python3
"""
host_ota.py -- Host-side Android Desktop A/B OTA Sideloader & Inspector for SIX.

Allows inspecting and applying an Android A/B ota.zip package directly to the
unified host disk image (./disk/x86/root) from outside SIX -- including live
while ./six is running!

Usage:
  python3 scripts/host_ota.py status [--disk disk/x86/root]
  python3 scripts/host_ota.py inspect <ota.zip>
  python3 scripts/host_ota.py sideload <ota.zip> [--disk disk/x86/root] [--slot b|a] [--no-activate]
"""

import argparse
import hashlib
import os
import struct
import sys
import zipfile

SECTOR_SIZE = 512
CRAU_BLOCK_SIZE = 4096
MIB = 1024 * 1024

# Partition offsets in unified disk/x86/root
SUPER_PART_OFFSET = 151 * MIB
UFS_PART_OFFSET = 223 * MIB

# Slice offsets inside p4:bin_storage (56 MiB)
SYSTEM_A_OFFSET = 0 * MIB
SYSTEM_SLICE_BYTES = 16 * MIB
VENDOR_A_OFFSET = 32 * MIB
VENDOR_SLICE_BYTES = 4 * MIB
SYSTEM_B_OFFSET = 36 * MIB
VENDOR_B_OFFSET = 52 * MIB

# UFS internal offsets inside p6:ufs0 (5 MiB)
UFS_HDR_OFFSET = 0x000000
UFS_BOOTA_VBMETA_OFFSET = 0x030000 + 512
UFS_BOOTB_VBMETA_OFFSET = 0x090000 + 512

UFS_HDR_MAGIC = 0x55465335
SIX_AVB_VBMETA_MAGIC = b"AVB0"

SIX_SLOT_FLAG_ACTIVE = 0x01
SIX_SLOT_FLAG_BOOTABLE = 0x02
SIX_SLOT_FLAG_SUCCESSFUL = 0x04
SIX_SLOT_FLAG_UNBOOTABLE = 0x08

SIX_CRAU_MAGIC = b"CrAU"
SIX_CRAU_VERSION = 2
SIX_CRAU_PART_SYSTEM = 0
SIX_CRAU_PART_VENDOR = 1
SIX_CRAU_OP_SOURCE_COPY = 0
SIX_CRAU_OP_REPLACE = 1
SIX_CRAU_OP_ZERO = 2

OP_NAMES = {
    SIX_CRAU_OP_SOURCE_COPY: "SOURCE_COPY",
    SIX_CRAU_OP_REPLACE: "REPLACE",
    SIX_CRAU_OP_ZERO: "ZERO",
}

PART_NAMES = {
    SIX_CRAU_PART_SYSTEM: "system",
    SIX_CRAU_PART_VENDOR: "vendor",
}

CRAU_OP_STRUCT = struct.Struct("<BBHIIII32s")
CRAU_HDR_STRUCT = struct.Struct("<4s7IQ32s32s32s64s32s16s8s")
UFS_HDR_STRUCT = struct.Struct("<IBBBBIII32s8sHH32sIIBB402s")
VBMETA_STRUCT = struct.Struct("<4sIBBBBIQ32s32s64s32s328s")


def cstr(b: bytes) -> str:
    return b.split(b"\x00", 1)[0].decode("ascii", errors="replace")


def format_flags(flags: int) -> str:
    names = []
    if flags & SIX_SLOT_FLAG_ACTIVE:
        names.append("ACTIVE")
    if flags & SIX_SLOT_FLAG_BOOTABLE:
        names.append("BOOTABLE")
    if flags & SIX_SLOT_FLAG_SUCCESSFUL:
        names.append("SUCCESSFUL")
    if flags & SIX_SLOT_FLAG_UNBOOTABLE:
        names.append("UNBOOTABLE")
    return " ".join(names) if names else "NONE"


def parse_kv(text: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        out[k.strip()] = v.strip()
    return out


def cmd_inspect(zip_path: str) -> int:
    if not os.path.exists(zip_path):
        print(f"host_ota: file not found: {zip_path}", file=sys.stderr)
        return 1

    with zipfile.ZipFile(zip_path, "r") as zf:
        meta_txt = zf.read("META-INF/com/android/metadata").decode("ascii", errors="replace")
        props_txt = zf.read("payload_properties.txt").decode("ascii", errors="replace")
        care_txt = zf.read("care_map.txt").decode("ascii", errors="replace")
        payload = zf.read("payload.bin")

    props = parse_kv(props_txt)
    calc_hash = hashlib.sha256(payload).hexdigest()
    hash_ok = calc_hash == props.get("FILE_HASH", "")

    (
        magic,
        version,
        hdr_size,
        num_ops,
        ops_size,
        blob_offset,
        blob_size,
        ota_type,
        rollback_idx,
        sys_root,
        vnd_root,
        manifest_sha,
        build_id_b,
        release_b,
        sec_patch_b,
        _,
    ) = CRAU_HDR_STRUCT.unpack(payload[: CRAU_HDR_STRUCT.size])

    ops_raw = payload[hdr_size : hdr_size + ops_size]
    manifest_ok = hashlib.sha256(ops_raw).digest() == manifest_sha

    print(f"=== Android Desktop A/B OTA Package Inspection: {zip_path} ({os.path.getsize(zip_path)} bytes) ===")
    print("META-INF/com/android/metadata:")
    for line in meta_txt.strip().splitlines():
        print(f"  {line}")
    print("\npayload_properties.txt:")
    for line in props_txt.strip().splitlines():
        print(f"  {line}")
    print(f"\nCrAU v{version} Payload Header ({len(payload)} bytes, SHA-256 verified={hash_ok}, Manifest verified={manifest_ok}):")
    print(f"  Payload Type:     {'FULL' if ota_type else 'DELTA (Block-Level SOURCE_COPY + REPLACE)'}")
    print(f"  Target Build ID:  {cstr(build_id_b)} (release {cstr(release_b)}, patch {cstr(sec_patch_b)})")
    print(f"  Rollback Index:   {rollback_idx}")
    print(f"  System Root Hash: {sys_root.hex()}")
    print(f"  Vendor Root Hash: {vnd_root.hex()}")
    print(f"  Install Ops ({num_ops} ops, {blob_size} bytes REPLACE blobs):")
    for i in range(num_ops):
        op_b = ops_raw[i * CRAU_OP_STRUCT.size : (i + 1) * CRAU_OP_STRUCT.size]
        part_id, op_type, nblks, src_blk, dst_blk, b_off, b_sz, d_sha = CRAU_OP_STRUCT.unpack(op_b)
        p_name = PART_NAMES.get(part_id, f"part_{part_id}")
        o_name = OP_NAMES.get(op_type, f"op_{op_type}")
        if op_type == SIX_CRAU_OP_REPLACE:
            print(
                f"    [{i:02d}] {p_name:<6} {o_name:<11} dst_4k={dst_blk:<4} count={nblks:<4} "
                f"blob=+{b_off} ({b_sz} B) sha256={d_sha.hex()[:16]}..."
            )
        else:
            print(
                f"    [{i:02d}] {p_name:<6} {o_name:<11} dst_4k={dst_blk:<4} count={nblks:<4} "
                f"src_4k={src_blk}"
            )
    return 0 if (hash_ok and manifest_ok and magic == SIX_CRAU_MAGIC) else 1


def read_ufs_and_vbmeta(fd: int):
    os.lseek(fd, UFS_PART_OFFSET + UFS_HDR_OFFSET, os.SEEK_SET)
    hdr_raw = os.read(fd, UFS_HDR_STRUCT.size)
    ufs_hdr = list(UFS_HDR_STRUCT.unpack(hdr_raw))

    os.lseek(fd, UFS_PART_OFFSET + UFS_BOOTA_VBMETA_OFFSET, os.SEEK_SET)
    vbm_a = list(VBMETA_STRUCT.unpack(os.read(fd, VBMETA_STRUCT.size)))

    os.lseek(fd, UFS_PART_OFFSET + UFS_BOOTB_VBMETA_OFFSET, os.SEEK_SET)
    vbm_b = list(VBMETA_STRUCT.unpack(os.read(fd, VBMETA_STRUCT.size)))

    return ufs_hdr, vbm_a, vbm_b


def cmd_status(disk_path: str) -> int:
    if not os.path.exists(disk_path):
        print(f"host_ota: disk not found: {disk_path}", file=sys.stderr)
        return 1
    fd = os.open(disk_path, os.O_RDONLY)
    try:
        ufs_hdr, vbm_a, vbm_b = read_ufs_and_vbmeta(fd)
    finally:
        os.close(fd)

    boot_lun_id = ufs_hdr[1]
    ota_pending_seq = ufs_hdr[13]
    ota_ack_seq = ufs_hdr[14]
    ota_pending_slot = ufs_hdr[15]
    ota_last_status = ufs_hdr[16]

    active_slot_str = "slot_b (_b)" if boot_lun_id == 2 else "slot_a (_a)"
    status_map = {0: "OK", 1: "ROLLED_BACK", 2: "ERROR"}

    print(f"=== Host-Side SIX Disk A/B OTA Status ({disk_path}) ===")
    print(f"UFS Active Boot LUN:  bBootLunID=0x{boot_lun_id:02x} -> {active_slot_str}")
    print(
        f"Host Sideload Sync:   pending_seq={ota_pending_seq} ack_seq={ota_ack_seq} "
        f"pending_slot={ota_pending_slot} last_status={status_map.get(ota_last_status, str(ota_last_status))}"
    )
    print(
        f"Slot A (/dev/ufsb):   [{format_flags(vbm_a[5])}] prio={vbm_a[3]} tries={vbm_a[4]} "
        f"rollback={vbm_a[7]} build={cstr(vbm_a[10])}"
    )
    print(f"  system_a root_hash: {vbm_a[8].hex()}")
    print(f"  vendor_a root_hash: {vbm_a[9].hex()}")
    print(
        f"Slot B (/dev/ufsc):   [{format_flags(vbm_b[5])}] prio={vbm_b[3]} tries={vbm_b[4]} "
        f"rollback={vbm_b[7]} build={cstr(vbm_b[10])}"
    )
    print(f"  system_b root_hash: {vbm_b[8].hex()}")
    print(f"  vendor_b root_hash: {vbm_b[9].hex()}")
    return 0


def cmd_sideload(
    zip_path: str,
    disk_path: str,
    slot_arg: str | None = None,
    no_activate: bool = False,
) -> int:
    if not os.path.exists(zip_path):
        print(f"host_ota: ota.zip not found: {zip_path}", file=sys.stderr)
        return 1
    if not os.path.exists(disk_path):
        print(f"host_ota: disk image not found: {disk_path}", file=sys.stderr)
        return 1

    with zipfile.ZipFile(zip_path, "r") as zf:
        props_txt = zf.read("payload_properties.txt").decode("ascii", errors="replace")
        payload = zf.read("payload.bin")

    props = parse_kv(props_txt)
    calc_file_hash = hashlib.sha256(payload).hexdigest()
    if props.get("FILE_HASH") and calc_file_hash != props["FILE_HASH"]:
        print(
            f"host_ota: payload.bin SHA-256 mismatch! expected={props['FILE_HASH']} got={calc_file_hash}",
            file=sys.stderr,
        )
        return 1

    (
        magic,
        version,
        hdr_size,
        num_ops,
        ops_size,
        blob_offset,
        blob_size,
        ota_type,
        rollback_idx,
        sys_root,
        vnd_root,
        manifest_sha,
        build_id_b,
        release_b,
        sec_patch_b,
        _,
    ) = CRAU_HDR_STRUCT.unpack(payload[: CRAU_HDR_STRUCT.size])

    if magic != SIX_CRAU_MAGIC or version != SIX_CRAU_VERSION:
        print("host_ota: invalid CrAU header magic or version", file=sys.stderr)
        return 1

    ops_raw = payload[hdr_size : hdr_size + ops_size]
    if hashlib.sha256(ops_raw).digest() != manifest_sha:
        print("host_ota: CrAU manifest SHA-256 verification failed!", file=sys.stderr)
        return 1

    blobs = payload[blob_offset : blob_offset + blob_size]
    build_id = cstr(build_id_b)
    release_str = cstr(release_b)

    fd = os.open(disk_path, os.O_RDWR)
    try:
        ufs_hdr, vbm_a, vbm_b = read_ufs_and_vbmeta(fd)
        active_lun = ufs_hdr[1] if ufs_hdr[1] in (1, 2) else 1

        if slot_arg in ("a", "_a", "0", "slot_a"):
            target_slot = 0
        elif slot_arg in ("b", "_b", "1", "slot_b"):
            target_slot = 1
        else:
            # Default to slot_b if slot_a is active, or parse TARGET_SLOT from payload_properties
            if props.get("TARGET_SLOT") == "_a":
                target_slot = 0
            elif props.get("TARGET_SLOT") == "_b":
                target_slot = 1
            else:
                target_slot = 1 if active_lun == 1 else 0

        # Read baseline source slices (system_a and vendor_a) for SOURCE_COPY ops
        os.lseek(fd, SUPER_PART_OFFSET + SYSTEM_A_OFFSET, os.SEEK_SET)
        src_sys = os.read(fd, SYSTEM_SLICE_BYTES)
        os.lseek(fd, SUPER_PART_OFFSET + VENDOR_A_OFFSET, os.SEEK_SET)
        src_vnd = os.read(fd, VENDOR_SLICE_BYTES)

        dst_sys = bytearray(SYSTEM_SLICE_BYTES)
        dst_vnd = bytearray(VENDOR_SLICE_BYTES)

        for i in range(num_ops):
            op_b = ops_raw[i * CRAU_OP_STRUCT.size : (i + 1) * CRAU_OP_STRUCT.size]
            part_id, op_type, nblks, src_blk, dst_blk, b_off, b_sz, d_sha = (
                CRAU_OP_STRUCT.unpack(op_b)
            )
            src_buf = src_sys if part_id == SIX_CRAU_PART_SYSTEM else src_vnd
            dst_buf = dst_sys if part_id == SIX_CRAU_PART_SYSTEM else dst_vnd
            dst_off = dst_blk * CRAU_BLOCK_SIZE
            byte_len = nblks * CRAU_BLOCK_SIZE

            if op_type == SIX_CRAU_OP_ZERO:
                dst_buf[dst_off : dst_off + byte_len] = b"\x00" * byte_len
            elif op_type == SIX_CRAU_OP_SOURCE_COPY:
                src_off = src_blk * CRAU_BLOCK_SIZE
                dst_buf[dst_off : dst_off + byte_len] = src_buf[src_off : src_off + byte_len]
            elif op_type == SIX_CRAU_OP_REPLACE:
                chunk = blobs[b_off : b_off + b_sz]
                if len(chunk) != byte_len or hashlib.sha256(chunk).digest() != d_sha:
                    print(f"host_ota: REPLACE op {i} blob SHA-256 mismatch!", file=sys.stderr)
                    return 1
                dst_buf[dst_off : dst_off + byte_len] = chunk
            else:
                print(f"host_ota: unknown CrAU op_type {op_type}", file=sys.stderr)
                return 1

        # Write reconstructed target system & vendor slices into p4:bin_storage (super)
        dst_sys_off = SUPER_PART_OFFSET + (SYSTEM_B_OFFSET if target_slot == 1 else SYSTEM_A_OFFSET)
        dst_vnd_off = SUPER_PART_OFFSET + (VENDOR_B_OFFSET if target_slot == 1 else VENDOR_A_OFFSET)

        os.lseek(fd, dst_sys_off, os.SEEK_SET)
        os.write(fd, dst_sys)
        os.lseek(fd, dst_vnd_off, os.SEEK_SET)
        os.write(fd, dst_vnd)

        # Write updated AVB0 vbmeta to target UFS Boot LUN (/dev/ufsc for slot_b, /dev/ufsb for slot_a)
        new_flags = SIX_SLOT_FLAG_BOOTABLE | SIX_SLOT_FLAG_SUCCESSFUL
        if not no_activate:
            new_flags |= SIX_SLOT_FLAG_ACTIVE

        new_vbm = VBMETA_STRUCT.pack(
            SIX_AVB_VBMETA_MAGIC,
            1,  # version
            target_slot,
            15,  # priority
            7,   # tries_remaining
            new_flags,
            0,   # reserved0
            rollback_idx,
            sys_root,
            vnd_root,
            build_id.encode("ascii")[:63].ljust(64, b"\x00"),
            release_str.encode("ascii")[:31].ljust(32, b"\x00"),
            b"\x00" * 328,
        )
        vbm_off = UFS_PART_OFFSET + (
            UFS_BOOTB_VBMETA_OFFSET if target_slot == 1 else UFS_BOOTA_VBMETA_OFFSET
        )
        os.lseek(fd, vbm_off, os.SEEK_SET)
        os.write(fd, new_vbm)

        # Trigger live slot activation (or cache refresh for --no-activate) via UFS persistent header
        ufs_hdr[0] = UFS_HDR_MAGIC
        if not no_activate:
            target_lun = 2 if target_slot == 1 else 1
            ufs_hdr[1] = target_lun
            ufs_hdr[13] = (ufs_hdr[13] + 1) & 0xFFFFFFFF  # ota_pending_seq++
            ufs_hdr[15] = target_lun                      # ota_pending_slot
        else:
            ufs_hdr[13] = (ufs_hdr[13] + 1) & 0xFFFFFFFF  # ota_pending_seq++
            ufs_hdr[15] = 0xFE                            # refresh buffers without switching slot
        os.lseek(fd, UFS_PART_OFFSET + UFS_HDR_OFFSET, os.SEEK_SET)
        os.write(fd, UFS_HDR_STRUCT.pack(*ufs_hdr))

        os.fsync(fd)
    finally:
        os.close(fd)

    print(
        f"[host_ota] Sideloaded {zip_path} -> slot_{'b' if target_slot else 'a'} "
        f"(build={build_id}, ops={num_ops}, blobs={blob_size} B, "
        f"sys_root={sys_root.hex()[:16]}..., vnd_root={vnd_root.hex()[:16]}...)"
    )
    if not no_activate:
        print(
            f"[host_ota] Signaled live slot switch to UFS controller "
            f"(bBootLunID=0x{2 if target_slot else 1:02x}, ota_pending_seq={ufs_hdr[13]})"
        )
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="SIX Host-Side Android A/B OTA Sideloader")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_st = sub.add_parser("status", help="Show A/B slot status from host disk image")
    p_st.add_argument("--disk", default="disk/x86/root", help="Path to unified disk image")

    p_in = sub.add_parser("inspect", help="Inspect an Android A/B ota.zip package")
    p_in.add_argument("zip_path", help="Path to ota.zip")

    p_sl = sub.add_parser("sideload", help="Sideload ota.zip into host disk image (live or offline)")
    p_sl.add_argument("zip_path", help="Path to ota.zip")
    p_sl.add_argument("--disk", default="disk/x86/root", help="Path to unified disk image")
    p_sl.add_argument("--slot", default=None, help="Target slot (a or b)")
    p_sl.add_argument("--no-activate", action="store_true", help="Stage without switching active slot")

    args = ap.parse_args()
    if args.cmd == "status":
        return cmd_status(args.disk)
    elif args.cmd == "inspect":
        return cmd_inspect(args.zip_path)
    elif args.cmd == "sideload":
        return cmd_sideload(args.zip_path, args.disk, args.slot, args.no_activate)
    return 1


if __name__ == "__main__":
    sys.exit(main())
