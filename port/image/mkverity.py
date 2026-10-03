#!/usr/bin/env python3
"""
mkverity.py -- Build SHA-256 Merkle trees and dm-verity superblocks for SIX's
56 MiB Android 'super' partition (p4:bin_storage on /dev/hdd) and emit
include/linux/verity_roothash.h.

Unified 56 MiB /dev/hdd ('super') A/B Layout (114,688 x 512-byte sectors):
  [  0 .. 16 MiB]  system_a    (sectors     0 ..  32767): 15 MiB EROFS + 1 MiB dm-verity
  [ 16 .. 32 MiB]  sarthak_bin (sectors 32768 ..  65535): 16 MiB go/erofs-verity
  [ 32 .. 36 MiB]  vendor_a    (sectors 65536 ..  73727):  3 MiB EROFS + 1 MiB dm-verity
  [ 36 .. 52 MiB]  system_b    (sectors 73728 .. 106495): 15 MiB EROFS + 1 MiB dm-verity
  [ 52 .. 56 MiB]  vendor_b    (sectors 106496.. 114687):  3 MiB EROFS + 1 MiB dm-verity
"""

import hashlib
import os
import struct
import sys

BLOCK_SIZE = 1024

SYSTEM_DATA_BLOCKS = 15360   # 15 MiB data area (480 L0 blocks, 15 L1 blocks)
SYSTEM_TOTAL_BLOCKS = 16384  # 16 MiB total slot size (32,768 sectors)

VENDOR_DATA_BLOCKS = 3072    # 3 MiB data area (96 L0 blocks, 3 active L1 blocks)
VENDOR_TOTAL_BLOCKS = 4096   # 4 MiB total slot size (8,192 sectors)

L2_OFFSET_BLOCKS = 1         # Block hs + 1 (1 block)
L1_OFFSET_BLOCKS = 2         # Blocks hs + 2 .. hs + 16 (15 blocks reserved)
L0_OFFSET_BLOCKS = 17        # Blocks hs + 17 .. (480 blocks for system, 96 for vendor)

SALT_STR = "SIX_DM_VERITY_SHA256_SALT_2026"
SALT = SALT_STR.encode("ascii") + b"\x00\x00"
assert len(SALT) == 32


def hash_block(data: bytes) -> bytes:
    assert len(data) == BLOCK_SIZE
    h = hashlib.sha256()
    h.update(SALT)
    h.update(data)
    return h.digest()


def build_verity_slice(raw: bytes, data_blocks: int, total_blocks: int) -> tuple[bytes, bytes]:
    expected_data_bytes = data_blocks * BLOCK_SIZE
    if len(raw) < expected_data_bytes:
        raw = raw + b"\x00" * (expected_data_bytes - len(raw))
    elif len(raw) > expected_data_bytes:
        raw = raw[:expected_data_bytes]

    num_l0 = data_blocks // 32
    num_l1 = data_blocks // 1024

    # 1. Compute data block hashes and pack into num_l0 Level-0 blocks (1024B each)
    l0_blocks = []
    for l0_idx in range(num_l0):
        buf = bytearray(BLOCK_SIZE)
        for slot in range(32):
            b_idx = l0_idx * 32 + slot
            blk = raw[b_idx * BLOCK_SIZE : (b_idx + 1) * BLOCK_SIZE]
            buf[slot * 32 : (slot + 1) * 32] = hash_block(blk)
        l0_blocks.append(bytes(buf))

    # 2. Compute Level-0 hashes and pack into 15 Level-1 blocks (1024B each)
    l1_blocks = []
    for l1_idx in range(15):
        buf = bytearray(BLOCK_SIZE)
        if l1_idx < num_l1:
            for slot in range(32):
                l0_idx = l1_idx * 32 + slot
                buf[slot * 32 : (slot + 1) * 32] = hash_block(l0_blocks[l0_idx])
        l1_blocks.append(bytes(buf))

    # 3. Compute active Level-1 hashes and pack into 1 Level-2 root block (1024B)
    l2_buf = bytearray(BLOCK_SIZE)
    for l1_idx in range(num_l1):
        l2_buf[l1_idx * 32 : (l1_idx + 1) * 32] = hash_block(l1_blocks[l1_idx])
    l2_block = bytes(l2_buf)

    # 4. Compute Root Hash over the Level-2 block
    root_hash = hash_block(l2_block)

    # 5. Build 1024-byte six_verity_sb superblock at block data_blocks
    sb_buf = bytearray(BLOCK_SIZE)
    alg_bytes = b"sha256" + b"\x00" * 26
    hdr = struct.pack(
        "<8s9I32s32s32s",
        b"verity\x00\x00",
        1,
        1,
        BLOCK_SIZE,
        BLOCK_SIZE,
        data_blocks,
        data_blocks,
        L0_OFFSET_BLOCKS,
        L1_OFFSET_BLOCKS,
        L2_OFFSET_BLOCKS,
        alg_bytes,
        SALT,
        root_hash,
    )
    sb_buf[: len(hdr)] = hdr

    meta_parts = [bytes(sb_buf), l2_block] + l1_blocks + l0_blocks
    meta_bytes = b"".join(meta_parts)
    total_bytes = total_blocks * BLOCK_SIZE
    rem_pad = total_bytes - (len(raw) + len(meta_bytes))
    assert rem_pad >= 0
    return raw + meta_bytes + (b"\x00" * rem_pad), root_hash


def patch_slot_b_build_prop(raw: bytes) -> bytes:
    """Update build.prop block in-place for slot_b while keeping exact 1024B alignment."""
    magic = b"# SIX_ANDROID_BUILD_PROP_V1\n"
    idx = raw.find(magic)
    if idx < 0 or (idx % BLOCK_SIZE) != 0:
        return raw
    blk = raw[idx : idx + BLOCK_SIZE]
    text = blk.rstrip(b"\x00").decode("utf-8", errors="ignore")
    text_b = (
        text.replace("ro.build.ab_ota_slot=_a", "ro.build.ab_ota_slot=_b")
        .replace("ro.vendor.build.ab_ota_slot=_a", "ro.vendor.build.ab_ota_slot=_b")
        .replace("ro.build.slot_suffix=_a", "ro.build.slot_suffix=_b")
    )
    new_bytes = text_b.encode("utf-8")[:BLOCK_SIZE]
    new_blk = new_bytes + b"\x00" * (BLOCK_SIZE - len(new_bytes))
    return raw[:idx] + new_blk + raw[idx + BLOCK_SIZE :]


def format_c_array(digest: bytes) -> str:
    rows = []
    for row in range(4):
        chunk = digest[row * 8 : (row + 1) * 8]
        rows.append("\t" + ", ".join(f"0x{b:02x}" for b in chunk))
    return ",\n".join(rows)


def main() -> int:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <bin_storage_img> <verity_roothash_h>", file=sys.stderr)
        return 2

    img_path = sys.argv[1]
    hdr_path = sys.argv[2]

    with open(img_path, "rb") as f:
        sys_a_raw = f.read()

    sys_b_raw = patch_slot_b_build_prop(sys_a_raw)

    sys_a_slice, sys_a_hash = build_verity_slice(sys_a_raw, SYSTEM_DATA_BLOCKS, SYSTEM_TOTAL_BLOCKS)
    sys_b_slice, sys_b_hash = build_verity_slice(sys_b_raw, SYSTEM_DATA_BLOCKS, SYSTEM_TOTAL_BLOCKS)

    sarthak_path = img_path + ".sarthak"
    sarthak_total_bytes = SYSTEM_TOTAL_BLOCKS * BLOCK_SIZE
    sarthak_raw = b"\x00" * sarthak_total_bytes
    sarthak_digest = b"\x00" * 32
    if os.path.exists(sarthak_path):
        with open(sarthak_path, "rb") as sf:
            s_data = sf.read()
        if len(s_data) >= 2 * BLOCK_SIZE:
            sarthak_digest = hashlib.sha256(s_data[BLOCK_SIZE : 2 * BLOCK_SIZE]).digest()
        if len(s_data) < sarthak_total_bytes:
            sarthak_raw = s_data + b"\x00" * (sarthak_total_bytes - len(s_data))
        else:
            sarthak_raw = s_data[:sarthak_total_bytes]
        os.unlink(sarthak_path)

    vendor_path = img_path + ".vendor"
    vendor_a_raw = b"\x00" * (VENDOR_DATA_BLOCKS * BLOCK_SIZE)
    if os.path.exists(vendor_path):
        with open(vendor_path, "rb") as vf:
            vendor_a_raw = vf.read()
        os.unlink(vendor_path)

    vendor_b_raw = patch_slot_b_build_prop(vendor_a_raw)
    vendor_a_slice, vendor_a_hash = build_verity_slice(vendor_a_raw, VENDOR_DATA_BLOCKS, VENDOR_TOTAL_BLOCKS)
    vendor_b_slice, vendor_b_hash = build_verity_slice(vendor_b_raw, VENDOR_DATA_BLOCKS, VENDOR_TOTAL_BLOCKS)

    # Write 56 MiB unified 'super' partition image:
    #   [0..16M] system_a + [16..32M] sarthak_bin + [32..36M] vendor_a + [36..52M] system_b + [52..56M] vendor_b
    with open(img_path, "wb") as f:
        f.write(sys_a_slice)
        f.write(sarthak_raw)
        f.write(vendor_a_slice)
        f.write(sys_b_slice)
        f.write(vendor_b_slice)

    sys_a_hex = sys_a_hash.hex()
    sys_b_hex = sys_b_hash.hex()
    vnd_a_hex = vendor_a_hash.hex()
    vnd_b_hex = vendor_b_hash.hex()
    sarthak_hex = sarthak_digest.hex()

    hdr_content = f"""/* Auto-generated by port/image/mkverity.py -- DO NOT EDIT */
#ifndef _LINUX_VERITY_ROOTHASH_H
#define _LINUX_VERITY_ROOTHASH_H

#define VERITY_BIN_DATA_BLOCKS        {SYSTEM_DATA_BLOCKS}UL
#define VERITY_BIN_DATA_SECTORS       {SYSTEM_DATA_BLOCKS * 2}UL
#define VERITY_BIN_HASH_START_BLOCK   {SYSTEM_DATA_BLOCKS}UL
#define VERITY_BIN_HASH_START_SECTOR  {SYSTEM_DATA_BLOCKS * 2}UL

#define VERITY_BIN_SALT_STR           "{SALT_STR}"
#define VERITY_BIN_ROOT_HASH_HEX      "{sys_a_hex}"
#define VERITY_BIN_B_ROOT_HASH_HEX    "{sys_b_hex}"

#define SARTHAK_BIN_START_BLOCK       {SYSTEM_TOTAL_BLOCKS}UL
#define SARTHAK_BIN_START_SECTOR      {SYSTEM_TOTAL_BLOCKS * 2}UL
#define SARTHAK_BIN_DATA_BLOCKS       {SYSTEM_DATA_BLOCKS}UL
#define SARTHAK_BIN_DATA_SECTORS      {SYSTEM_DATA_BLOCKS * 2}UL
#define SARTHAK_EROFS_ROOT_DIGEST_HEX "{sarthak_hex}"

#define VERITY_VENDOR_DATA_BLOCKS     {VENDOR_DATA_BLOCKS}UL
#define VERITY_VENDOR_DATA_SECTORS    {VENDOR_DATA_BLOCKS * 2}UL
#define VERITY_VENDOR_A_START_SECTOR  65536UL
#define VERITY_VENDOR_A_HASH_SECTOR   71680UL
#define VERITY_VENDOR_ROOT_HASH_HEX   "{vnd_a_hex}"

#define VERITY_SYSTEM_B_START_SECTOR  73728UL
#define VERITY_SYSTEM_B_HASH_SECTOR   104448UL

#define VERITY_VENDOR_B_START_SECTOR  106496UL
#define VERITY_VENDOR_B_HASH_SECTOR   112640UL
#define VERITY_VENDOR_B_ROOT_HASH_HEX "{vnd_b_hex}"

static const unsigned char verity_bin_root_hash[32] = {{
{format_c_array(sys_a_hash)}
}};

static const unsigned char verity_bin_b_root_hash[32] = {{
{format_c_array(sys_b_hash)}
}};

static const unsigned char verity_vendor_root_hash[32] = {{
{format_c_array(vendor_a_hash)}
}};

static const unsigned char verity_vendor_b_root_hash[32] = {{
{format_c_array(vendor_b_hash)}
}};

static const unsigned char sarthak_erofs_root_digest[32] = {{
{format_c_array(sarthak_digest)}
}};

#endif /* _LINUX_VERITY_ROOTHASH_H */
"""

    old_content = None
    if os.path.exists(hdr_path):
        with open(hdr_path, "r", encoding="utf-8") as f:
            old_content = f.read()

    if old_content != hdr_content:
        os.makedirs(os.path.dirname(hdr_path), exist_ok=True)
        with open(hdr_path, "w", encoding="utf-8") as f:
            f.write(hdr_content)

    print(
        f"mkverity: built 56 MiB super image {img_path} "
        f"(sys_a={sys_a_hex[:12]}.., vnd_a={vnd_a_hex[:12]}.., "
        f"sys_b={sys_b_hex[:12]}.., vnd_b={vnd_b_hex[:12]}.., "
        f"sarthak={sarthak_hex[:12]}..)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
