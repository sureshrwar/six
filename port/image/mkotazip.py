#!/usr/bin/env python3
"""
mkotazip.py -- Build an Android Desktop / AOSP Seamless A/B OTA package (ota.zip)
containing an uncompressed ("ZIP_STORED") CrAU v2 payload.bin, payload_properties.txt,
care_map.txt, and META-INF/com/android/metadata.

Supports both block-level Delta OTA (SOURCE_COPY + REPLACE + ZERO) and Full OTA
(REPLACE + ZERO), and can be applied either:
  1. Inside SIX guest:  ota install /path/to/ota.zip
                        update_engine_client --update --payload=file:///path/to/ota.zip
  2. From Linux host while SIX is running (or offline):
                        python3 scripts/host_ota.py sideload /path/to/ota.zip
"""

import argparse
import hashlib
import os
import struct
import sys
import zipfile

SECTOR_SIZE = 512
VERITY_BLOCK_SIZE = 1024
CRAU_BLOCK_SIZE = 4096
MIB = 1024 * 1024

# p4:bin_storage (super) offset in unified disk/x86/root
SUPER_PART_OFFSET_BYTES = 151 * MIB

# Sub-partition offsets inside p4:bin_storage (56 MiB)
SYSTEM_A_OFFSET = 0 * MIB
SYSTEM_SLICE_BYTES = 16 * MIB
SYSTEM_DATA_BYTES = 15 * MIB
SYSTEM_HASH_OFFSET = 15 * MIB

VENDOR_A_OFFSET = 32 * MIB
VENDOR_SLICE_BYTES = 4 * MIB
VENDOR_DATA_BYTES = 3 * MIB
VENDOR_HASH_OFFSET = 3 * MIB

SIX_CRAU_MAGIC = b"CrAU"
SIX_CRAU_VERSION = 2

SIX_CRAU_PART_SYSTEM = 0
SIX_CRAU_PART_VENDOR = 1

SIX_CRAU_OP_SOURCE_COPY = 0
SIX_CRAU_OP_REPLACE = 1
SIX_CRAU_OP_ZERO = 2

CRAU_OP_STRUCT = struct.Struct("<BBHIIII32s")
CRAU_HDR_STRUCT = struct.Struct("<4s7IQ32s32s32s64s32s16s8s")
VERITY_SB_STRUCT = struct.Struct("<8s9I32s32s32s")


def salted_sha256_1k(salt: bytes, block_1k: bytes) -> bytes:
    return hashlib.sha256(salt + block_1k).digest()


def find_build_prop_1k_block(slice_buf: bytearray) -> int:
    marker = b"# SIX_ANDROID_BUILD_PROP_V1\n"
    for blk in range(1, 64):
        off = blk * VERITY_BLOCK_SIZE
        if slice_buf[off : off + len(marker)] == marker:
            return blk
    return -1


def format_build_prop_1k(
    is_vendor: bool,
    slot_suffix: str,
    build_id: str,
    ota_version: int,
    security_patch: str,
) -> bytes:
    inc = 1 + ota_version
    if not is_vendor:
        text = (
            "# SIX_ANDROID_BUILD_PROP_V1\n"
            "ro.build.Partition=system\n"
            f"ro.build.Slot={slot_suffix}\n"
            f"ro.build.id={build_id}\n"
            f"ro.build.version.incremental=20261003.{inc:04d}\n"
            "ro.build.version.release=16\n"
            f"ro.build.version.security_patch={security_patch}\n"
            f"ro.build.fingerprint=google/six_x86/six:16/{build_id}/20261003.{inc:04d}:user/release-keys\n"
            "ro.product.system.brand=google\n"
            "ro.product.system.name=six_x86\n"
            "ro.product.system.device=six\n"
            f"ro.boot.slot_suffix={slot_suffix}\n"
            "ro.boot.verifiedbootstate=green\n"
            "ro.boot.veritymode=enforcing\n"
            f"ro.ota.version={ota_version}\n"
        ).encode("ascii")
    else:
        text = (
            "# SIX_ANDROID_BUILD_PROP_V1\n"
            "ro.build.Partition=vendor\n"
            f"ro.build.Slot={slot_suffix}\n"
            f"ro.vendor.build.id={build_id}\n"
            f"ro.vendor.build.version.incremental=20261003.{inc:04d}\n"
            "ro.vendor.build.version.release=16\n"
            f"ro.vendor.build.security_patch={security_patch}\n"
            f"ro.vendor.build.fingerprint=google/six_x86/six:16/{build_id}/20261003.{inc:04d}:user/release-keys\n"
            "ro.product.vendor.brand=google\n"
            "ro.product.vendor.name=six_x86\n"
            "ro.product.vendor.device=six\n"
            f"ro.boot.slot_suffix={slot_suffix}\n"
            f"ro.ota.version={ota_version}\n"
        ).encode("ascii")

    buf = bytearray(b"#" * VERITY_BLOCK_SIZE)
    n = min(len(text), VERITY_BLOCK_SIZE - 1)
    buf[:n] = text[:n]
    buf[VERITY_BLOCK_SIZE - 1] = ord("\n")
    return bytes(buf)


def update_verity_1k_and_merkle_spine(
    slice_buf: bytearray, hash_offset: int, blk_nr: int, new_1k: bytes
) -> bytes:
    sb_raw = bytes(slice_buf[hash_offset : hash_offset + VERITY_SB_STRUCT.size])
    (
        magic,
        version,
        hash_type,
        data_bs,
        hash_bs,
        data_blocks,
        hash_start_blk,
        l0_off,
        l1_off,
        l2_off,
        algo,
        salt,
        old_root,
    ) = VERITY_SB_STRUCT.unpack(sb_raw)
    if magic != b"verity\x00\x00":
        raise ValueError("Invalid dm-verity superblock magic in source slice")

    # 1. Write new 1 KB data block
    data_off = blk_nr * VERITY_BLOCK_SIZE
    slice_buf[data_off : data_off + VERITY_BLOCK_SIZE] = new_1k
    d_hash = salted_sha256_1k(salt, new_1k)

    # 2. Update Level-0 leaf block
    l0_blk_off = hash_offset + (17 + (blk_nr >> 5)) * VERITY_BLOCK_SIZE
    l0_slot_off = l0_blk_off + ((blk_nr & 31) * 32)
    slice_buf[l0_slot_off : l0_slot_off + 32] = d_hash
    l0_hash = salted_sha256_1k(
        salt, bytes(slice_buf[l0_blk_off : l0_blk_off + VERITY_BLOCK_SIZE])
    )

    # 3. Update Level-1 interior block
    l1_blk_off = hash_offset + (2 + (blk_nr >> 10)) * VERITY_BLOCK_SIZE
    l1_slot_off = l1_blk_off + (((blk_nr >> 5) & 31) * 32)
    slice_buf[l1_slot_off : l1_slot_off + 32] = l0_hash
    l1_hash = salted_sha256_1k(
        salt, bytes(slice_buf[l1_blk_off : l1_blk_off + VERITY_BLOCK_SIZE])
    )

    # 4. Update Level-2 root block
    l2_blk_off = hash_offset + 1 * VERITY_BLOCK_SIZE
    l2_slot_off = l2_blk_off + ((blk_nr >> 10) * 32)
    slice_buf[l2_slot_off : l2_slot_off + 32] = l1_hash
    root_hash = salted_sha256_1k(
        salt, bytes(slice_buf[l2_blk_off : l2_blk_off + VERITY_BLOCK_SIZE])
    )

    # 5. Update verity superblock root_hash
    new_sb = VERITY_SB_STRUCT.pack(
        magic,
        version,
        hash_type,
        data_bs,
        hash_bs,
        data_blocks,
        hash_start_blk,
        l0_off,
        l1_off,
        l2_off,
        algo,
        salt,
        root_hash,
    )
    slice_buf[hash_offset : hash_offset + VERITY_SB_STRUCT.size] = new_sb
    return root_hash


def diff_partition_4k(
    part_id: int,
    src_slice: bytes,
    dst_slice: bytes,
    is_full: bool,
    blobs: bytearray,
) -> list[bytes]:
    assert len(src_slice) == len(dst_slice)
    assert len(dst_slice) % CRAU_BLOCK_SIZE == 0
    total_4k = len(dst_slice) // CRAU_BLOCK_SIZE
    zero_4k = b"\x00" * CRAU_BLOCK_SIZE

    ops_raw: list[bytes] = []
    b = 0
    while b < total_4k:
        dst_blk = dst_slice[b * CRAU_BLOCK_SIZE : (b + 1) * CRAU_BLOCK_SIZE]
        src_blk = src_slice[b * CRAU_BLOCK_SIZE : (b + 1) * CRAU_BLOCK_SIZE]

        if dst_blk == zero_4k:
            op_type = SIX_CRAU_OP_ZERO
        elif (not is_full) and dst_blk == src_blk:
            op_type = SIX_CRAU_OP_SOURCE_COPY
        else:
            op_type = SIX_CRAU_OP_REPLACE

        start_b = b
        b += 1
        while b < total_4k and (b - start_b) < 4096:
            nxt_dst = dst_slice[b * CRAU_BLOCK_SIZE : (b + 1) * CRAU_BLOCK_SIZE]
            nxt_src = src_slice[b * CRAU_BLOCK_SIZE : (b + 1) * CRAU_BLOCK_SIZE]
            if nxt_dst == zero_4k:
                nxt_type = SIX_CRAU_OP_ZERO
            elif (not is_full) and nxt_dst == nxt_src:
                nxt_type = SIX_CRAU_OP_SOURCE_COPY
            else:
                nxt_type = SIX_CRAU_OP_REPLACE
            if nxt_type != op_type:
                break
            b += 1

        num_blocks = b - start_b
        if op_type == SIX_CRAU_OP_REPLACE:
            chunk = dst_slice[
                start_b * CRAU_BLOCK_SIZE : (start_b + num_blocks) * CRAU_BLOCK_SIZE
            ]
            blob_off = len(blobs)
            blob_sz = len(chunk)
            blobs.extend(chunk)
            digest = hashlib.sha256(chunk).digest()
        else:
            blob_off = 0
            blob_sz = 0
            digest = b"\x00" * 32

        ops_raw.append(
            CRAU_OP_STRUCT.pack(
                part_id,
                op_type,
                num_blocks,
                start_b if op_type == SIX_CRAU_OP_SOURCE_COPY else 0,
                start_b,
                blob_off,
                blob_sz,
                digest,
            )
        )

    return ops_raw


def load_base_slices(disk_path: str) -> tuple[bytes, bytes]:
    if not os.path.exists(disk_path):
        raise FileNotFoundError(f"Base disk image not found: {disk_path}")
    sz = os.path.getsize(disk_path)
    base_off = SUPER_PART_OFFSET_BYTES if sz >= 200 * MIB else 0
    with open(disk_path, "rb") as f:
        f.seek(base_off + SYSTEM_A_OFFSET)
        sys_a = f.read(SYSTEM_SLICE_BYTES)
        f.seek(base_off + VENDOR_A_OFFSET)
        vnd_a = f.read(VENDOR_SLICE_BYTES)
    if len(sys_a) != SYSTEM_SLICE_BYTES or len(vnd_a) != VENDOR_SLICE_BYTES:
        raise ValueError(f"Incomplete super partition slices in {disk_path}")
    return sys_a, vnd_a


def build_ota_zip(
    disk_path: str,
    out_zip: str,
    build_id: str,
    slot_suffix: str = "_b",
    ota_version: int = 2,
    rollback_index: int = 2,
    security_patch: str = "2026-10-05",
    is_full: bool = False,
    corrupt_verity: bool = False,
) -> dict:
    sys_a, vnd_a = load_base_slices(disk_path)
    sys_dst = bytearray(sys_a)
    vnd_dst = bytearray(vnd_a)

    sys_prop_blk = find_build_prop_1k_block(sys_dst)
    vnd_prop_blk = find_build_prop_1k_block(vnd_dst)
    if sys_prop_blk < 0 or vnd_prop_blk < 0:
        raise ValueError("Could not locate # SIX_ANDROID_BUILD_PROP_V1 in base slices")

    sys_prop_1k = format_build_prop_1k(
        False, slot_suffix, build_id, ota_version, security_patch
    )
    vnd_prop_1k = format_build_prop_1k(
        True, slot_suffix, build_id, ota_version, security_patch
    )

    sys_root_hash = update_verity_1k_and_merkle_spine(
        sys_dst, SYSTEM_HASH_OFFSET, sys_prop_blk, sys_prop_1k
    )
    vnd_root_hash = update_verity_1k_and_merkle_spine(
        vnd_dst, VENDOR_HASH_OFFSET, vnd_prop_blk, vnd_prop_1k
    )

    if corrupt_verity:
        # Tamper with EROFS superblock (1 KB block 1) in target system without updating Merkle tree
        for i in range(1024, 1028):
            sys_dst[i] ^= 0xFF

    blobs = bytearray()
    ops_raw = []
    ops_raw.extend(
        diff_partition_4k(SIX_CRAU_PART_SYSTEM, sys_a, bytes(sys_dst), is_full, blobs)
    )
    ops_raw.extend(
        diff_partition_4k(SIX_CRAU_PART_VENDOR, vnd_a, bytes(vnd_dst), is_full, blobs)
    )

    ops_bytes = b"".join(ops_raw)
    manifest_sha256 = hashlib.sha256(ops_bytes).digest()
    release_str = f"16 (20261003.{1 + ota_version:04d})"

    hdr_bytes = CRAU_HDR_STRUCT.pack(
        SIX_CRAU_MAGIC,
        SIX_CRAU_VERSION,
        CRAU_HDR_STRUCT.size,
        len(ops_raw),
        len(ops_bytes),
        CRAU_HDR_STRUCT.size + len(ops_bytes),
        len(blobs),
        1 if is_full else 0,
        rollback_index,
        sys_root_hash,
        vnd_root_hash,
        manifest_sha256,
        build_id.encode("ascii")[:63].ljust(64, b"\x00"),
        release_str.encode("ascii")[:31].ljust(32, b"\x00"),
        security_patch.encode("ascii")[:15].ljust(16, b"\x00"),
        b"\x00" * 8,
    )

    payload_bin = hdr_bytes + ops_bytes + bytes(blobs)
    payload_sha256_hex = hashlib.sha256(payload_bin).hexdigest()
    manifest_sha256_hex = manifest_sha256.hex()

    payload_props = (
        f"FILE_HASH={payload_sha256_hex}\n"
        f"FILE_SIZE={len(payload_bin)}\n"
        f"METADATA_HASH={manifest_sha256_hex}\n"
        f"METADATA_SIZE={len(hdr_bytes) + len(ops_bytes)}\n"
        f"BUILD_ID={build_id}\n"
        f"TARGET_SLOT={slot_suffix}\n"
        f"OTA_TYPE={'FULL' if is_full else 'DELTA'}\n"
        f"ROLLBACK_INDEX={rollback_index}\n"
        f"SYSTEM_ROOT_HASH={sys_root_hash.hex()}\n"
        f"VENDOR_ROOT_HASH={vnd_root_hash.hex()}\n"
    ).encode("ascii")

    care_map = (
        "system\n"
        "0,3840\n"
        "vendor\n"
        "0,768\n"
    ).encode("ascii")

    # Compute exact streaming offsets inside ZIP_STORED archive:
    # Each local file header is 30 + len(filename) bytes (no extra field).
    meta_name = "META-INF/com/android/metadata"
    props_name = "payload_properties.txt"
    care_name = "care_map.txt"
    payload_name = "payload.bin"

    # Build metadata with exact byte offsets
    def make_metadata(p_off: int, pr_off: int, c_off: int) -> bytes:
        return (
            "ota-type=AB\n"
            "ota-required-cache=0\n"
            "post-build=" + build_id + "\n"
            "post-sdk-level=35\n"
            "post-security-patch-level=" + security_patch + "\n"
            "pre-device=six_x86\n"
            f"ota-streaming-property-files="
            f"payload.bin:{p_off}:{len(payload_bin)},"
            f"payload_properties.txt:{pr_off}:{len(payload_props)},"
            f"care_map.txt:{c_off}:{len(care_map)}\n"
        ).encode("ascii")

    # Iterate twice so metadata length stabilizes with exact offsets
    meta_bytes = make_metadata(0, 0, 0)
    for _ in range(3):
        off_meta = 30 + len(meta_name)
        off_props = off_meta + len(meta_bytes) + 30 + len(props_name)
        off_care = off_props + len(payload_props) + 30 + len(care_name)
        off_payload = off_care + len(care_map) + 30 + len(payload_name)
        meta_bytes = make_metadata(off_payload, off_props, off_care)

    os.makedirs(os.path.dirname(os.path.abspath(out_zip)), exist_ok=True)
    with zipfile.ZipFile(out_zip, "w", compression=zipfile.ZIP_STORED) as zf:
        for name, data in [
            (meta_name, meta_bytes),
            (props_name, payload_props),
            (care_name, care_map),
            (payload_name, payload_bin),
        ]:
            zi = zipfile.ZipInfo(name, date_time=(2026, 10, 4, 12, 0, 0))
            zi.compress_type = zipfile.ZIP_STORED
            zi.external_attr = 0o644 << 16
            zf.writestr(zi, data)

    return {
        "out_zip": out_zip,
        "zip_size": os.path.getsize(out_zip),
        "payload_size": len(payload_bin),
        "payload_sha256": payload_sha256_hex,
        "num_ops": len(ops_raw),
        "blob_size": len(blobs),
        "build_id": build_id,
        "sys_root_hash": sys_root_hash.hex(),
        "vnd_root_hash": vnd_root_hash.hex(),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Build Android Desktop A/B ota.zip package for SIX")
    ap.add_argument("--disk", default="disk/x86/root", help="Path to unified disk or .bin_part.img")
    ap.add_argument("-o", "--out", default="port/image/ota.zip", help="Output ota.zip path")
    ap.add_argument("--build-id", default="SIX.261004.002.B2", help="Target ro.build.id")
    ap.add_argument("--slot", default="_b", choices=["_a", "_b"], help="Target slot suffix")
    ap.add_argument("--ota-version", type=int, default=2, help="Target ro.ota.version")
    ap.add_argument("--rollback-index", type=int, default=2, help="Target AVB rollback index")
    ap.add_argument("--security-patch", default="2026-10-05", help="Security patch date")
    ap.add_argument("--type", default="delta", choices=["delta", "full"], help="Delta or full payload")
    ap.add_argument("--corrupt-verity", action="store_true", help="Tamper with target system block 1 to test rollback")
    args = ap.parse_args()

    info = build_ota_zip(
        disk_path=args.disk,
        out_zip=args.out,
        build_id=args.build_id,
        slot_suffix=args.slot,
        ota_version=args.ota_version,
        rollback_index=args.rollback_index,
        security_patch=args.security_patch,
        is_full=(args.type == "full"),
        corrupt_verity=args.corrupt_verity,
    )
    print(
        f"mkotazip: built {info['out_zip']} ({info['zip_size']} bytes, "
        f"type={args.type.upper()}, ops={info['num_ops']}, blobs={info['blob_size']} B, "
        f"build={info['build_id']}, sys_root={info['sys_root_hash'][:16]}...)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
