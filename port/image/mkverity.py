#!/usr/bin/env python3
"""
mkverity.py -- Build a SHA-256 Merkle tree and dm-verity superblock for SIX's
/bin disk image (disk/x86/bin_storage) and emit include/linux/verity_roothash.h.

Disk Layout (16 MB total = 16384 x 1024-byte blocks = 32768 x 512-byte sectors):
  Blocks  0 .. 15359 (15 MB): ext4 read-only /bin filesystem (15360 x 1 KB blocks)
  Block   15360      (1 KB):  six_verity_sb superblock (magic "verity\\0\\0", version 1)
  Block   15361      (1 KB):  Level 2 (Root) Merkle hash block (15 x 32B hashes + zero pad)
  Blocks  15362..15376 (15 KB): Level 1 interior Merkle hash blocks (15 blocks x 32 hashes)
  Blocks  15377..15856 (480 KB): Level 0 leaf Merkle hash blocks (480 blocks x 32 hashes)
  Blocks  15857..16383 (527 KB): Reserved / zero padding to 16 MB
"""

import hashlib
import os
import struct
import sys

BLOCK_SIZE = 1024
DATA_BLOCKS = 15360          # 15 MB data area (divisible by 32*32 = 1024: 15 * 32 * 32)
TOTAL_BLOCKS = 16384         # 16 MB total disk image
HASH_START_BLOCK = 15360
L2_OFFSET_BLOCKS = 1         # Block 15361 (1 block)
L1_OFFSET_BLOCKS = 2         # Blocks 15362 .. 15376 (15 blocks)
L0_OFFSET_BLOCKS = 17        # Blocks 15377 .. 15856 (480 blocks)

SALT_STR = "SIX_DM_VERITY_SHA256_SALT_2026"
SALT = SALT_STR.encode("ascii") + b"\x00\x00"
assert len(SALT) == 32


def hash_block(data: bytes) -> bytes:
    assert len(data) == BLOCK_SIZE
    h = hashlib.sha256()
    h.update(SALT)
    h.update(data)
    return h.digest()


def main() -> int:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <bin_storage_img> <verity_roothash_h>", file=sys.stderr)
        return 2

    img_path = sys.argv[1]
    hdr_path = sys.argv[2]

    with open(img_path, "rb") as f:
        raw = f.read()

    expected_data_bytes = DATA_BLOCKS * BLOCK_SIZE
    if len(raw) < expected_data_bytes:
        raw = raw + b"\x00" * (expected_data_bytes - len(raw))
    elif len(raw) > expected_data_bytes:
        raw = raw[:expected_data_bytes]

    # 1. Compute 15,360 data block hashes and pack into 480 Level-0 blocks (each 1024 bytes)
    l0_blocks = []
    for l0_idx in range(480):
        buf = bytearray(BLOCK_SIZE)
        for slot in range(32):
            b_idx = l0_idx * 32 + slot
            blk = raw[b_idx * BLOCK_SIZE : (b_idx + 1) * BLOCK_SIZE]
            digest = hash_block(blk)
            buf[slot * 32 : (slot + 1) * 32] = digest
        l0_blocks.append(bytes(buf))

    # 2. Compute 480 Level-0 hashes and pack into 15 Level-1 blocks (each 1024 bytes)
    l1_blocks = []
    for l1_idx in range(15):
        buf = bytearray(BLOCK_SIZE)
        for slot in range(32):
            l0_idx = l1_idx * 32 + slot
            digest = hash_block(l0_blocks[l0_idx])
            buf[slot * 32 : (slot + 1) * 32] = digest
        l1_blocks.append(bytes(buf))

    # 3. Compute 15 Level-1 hashes and pack into 1 Level-2 block (1024 bytes, slots 15..31 zeroed)
    l2_buf = bytearray(BLOCK_SIZE)
    for l1_idx in range(15):
        digest = hash_block(l1_blocks[l1_idx])
        l2_buf[l1_idx * 32 : (l1_idx + 1) * 32] = digest
    l2_block = bytes(l2_buf)

    # 4. Compute the Root Hash over the Level-2 block
    root_hash = hash_block(l2_block)
    root_hash_hex = root_hash.hex()

    # 5. Build the 1024-byte verity superblock at HASH_START_BLOCK (15360)
    # struct six_verity_sb layout:
    #   char     magic[8];               // "verity\0\0"
    #   uint32_t version;                // 1
    #   uint32_t hash_type;              // 1 (SHA-256)
    #   uint32_t data_block_size;        // 1024
    #   uint32_t hash_block_size;        // 1024
    #   uint32_t data_blocks;            // 15360
    #   uint32_t hash_start_block;       // 15360
    #   uint32_t l0_offset_blocks;       // 17
    #   uint32_t l1_offset_blocks;       // 2
    #   uint32_t l2_offset_blocks;       // 1
    #   char     algorithm[32];          // "sha256"
    #   uint8_t  salt[32];               // SALT
    #   uint8_t  root_hash[32];          // root_hash
    sb_buf = bytearray(BLOCK_SIZE)
    alg_bytes = b"sha256" + b"\x00" * 26
    hdr = struct.pack(
        "<8s9I32s32s32s",
        b"verity\x00\x00",
        1,
        1,
        BLOCK_SIZE,
        BLOCK_SIZE,
        DATA_BLOCKS,
        HASH_START_BLOCK,
        L0_OFFSET_BLOCKS,
        L1_OFFSET_BLOCKS,
        L2_OFFSET_BLOCKS,
        alg_bytes,
        SALT,
        root_hash,
    )
    sb_buf[: len(hdr)] = hdr

    # 6. Assemble the full 16 MB image
    meta_parts = [bytes(sb_buf), l2_block] + l1_blocks + l0_blocks
    meta_bytes = b"".join(meta_parts)
    total_bytes = TOTAL_BLOCKS * BLOCK_SIZE
    rem_pad = total_bytes - (len(raw) + len(meta_bytes))
    assert rem_pad >= 0

    with open(img_path, "wb") as f:
        f.write(raw)
        f.write(meta_bytes)
        f.write(b"\x00" * rem_pad)

    # 7. Format include/linux/verity_roothash.h (only overwrite if changed)
    c_bytes_rows = []
    for row in range(4):
        chunk = root_hash[row * 8 : (row + 1) * 8]
        c_bytes_rows.append("\t" + ", ".join(f"0x{b:02x}" for b in chunk))
    c_array_body = ",\n".join(c_bytes_rows)

    hdr_content = f"""/* Auto-generated by port/image/mkverity.py -- DO NOT EDIT */
#ifndef _LINUX_VERITY_ROOTHASH_H
#define _LINUX_VERITY_ROOTHASH_H

#define VERITY_BIN_DATA_BLOCKS       {DATA_BLOCKS}UL
#define VERITY_BIN_DATA_SECTORS      {DATA_BLOCKS * 2}UL
#define VERITY_BIN_HASH_START_BLOCK  {HASH_START_BLOCK}UL
#define VERITY_BIN_HASH_START_SECTOR {HASH_START_BLOCK * 2}UL

#define VERITY_BIN_SALT_STR          "{SALT_STR}"
#define VERITY_BIN_ROOT_HASH_HEX     "{root_hash_hex}"

static const unsigned char verity_bin_root_hash[32] = {{
{c_array_body}
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
        f"mkverity: built SHA-256 Merkle tree for {img_path} "
        f"(data={DATA_BLOCKS} blocks, hash_start={HASH_START_BLOCK}, root_hash={root_hash_hex[:16]}...)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
