#!/usr/bin/env python3
"""
mksingledisk.py -- Manage SIX's unified GPT-partitioned single host disk image
(disk/x86/root, 215 MiB = 440,320 sectors, 11 partitions).

Usage:
  python3 port/image/mksingledisk.py init <disk_path>
  python3 port/image/mksingledisk.py write-part <disk_path> <part_name> <src_img>
  python3 port/image/mksingledisk.py zero-part <disk_path> <part_name>
  python3 port/image/mksingledisk.py has-part <disk_path> <part_name>
  python3 port/image/mksingledisk.py info <disk_path>
"""

import os
import struct
import sys
import uuid
import zlib

SECTOR_SIZE = 512
MIB = 1024 * 1024
TOTAL_MIB = 239
TOTAL_BYTES = TOTAL_MIB * MIB
TOTAL_SECTORS = TOTAL_BYTES // SECTOR_SIZE

# Standard GPT Partition Type GUIDs
GUID_LINUX_FS = uuid.UUID("0fc63daf-8483-4772-8e79-3d69d8477de4")
GUID_MS_BASIC_DATA = uuid.UUID("ebd0a0a2-b9e5-4433-87c0-68b6b72699c7")
DISK_GUID = uuid.UUID("53495830-2026-4000-8000-000000000000")

# (part_num, name, start_mib, size_mib, type_guid)
PARTITIONS = [
    (1,  "root",          1,   50, GUID_LINUX_FS),
    (2,  "aux_storage-1", 51,  50, GUID_MS_BASIC_DATA),
    (3,  "aux_storage-2", 101, 50, GUID_LINUX_FS),
    (4,  "bin_storage",   151, 56, GUID_LINUX_FS),
    (5,  "nvme0n1",       207, 16, GUID_LINUX_FS),
    (6,  "ufs0",          223, 5,  GUID_LINUX_FS),
    (7,  "usb_ext2",      228, 2,  GUID_LINUX_FS),
    (8,  "usb_ext4",      230, 2,  GUID_LINUX_FS),
    (9,  "usb_erofs",     232, 2,  GUID_LINUX_FS),
    (10, "usb_ntfs",      234, 2,  GUID_MS_BASIC_DATA),
    (11, "usb_crypt",     236, 2,  GUID_LINUX_FS),
]

PART_BY_NAME = {name: (idx, start_mib, size_mib, tguid) for idx, name, start_mib, size_mib, tguid in PARTITIONS}


def guid_to_mixed_endian(u: uuid.UUID) -> bytes:
    return u.bytes_le


def build_protective_mbr(total_sectors: int) -> bytes:
    mbr = bytearray(SECTOR_SIZE)
    # Partition 1 entry at offset 446: Protective MBR (type 0xEE) covering LBA 1 .. total_sectors-1
    start_lba = 1
    num_sectors = min(total_sectors - 1, 0xFFFFFFFF)
    mbr[446 : 446 + 16] = struct.pack(
        "<BBBBBBBBII",
        0x00,       # status
        0x00, 0x02, 0x00,  # start CHS (0/0/2)
        0xEE,       # partition type: GPT Protective MBR
        0xFE, 0xFF, 0xFF,  # end CHS
        start_lba,
        num_sectors,
    )
    mbr[510:512] = b"\x55\xaa"
    return bytes(mbr)


def build_gpt_entries() -> bytes:
    num_entries = 128
    entry_size = 128
    buf = bytearray(num_entries * entry_size)

    for idx, name, start_mib, size_mib, tguid in PARTITIONS:
        start_lba = (start_mib * MIB) // SECTOR_SIZE
        end_lba = ((start_mib + size_mib) * MIB) // SECTOR_SIZE - 1
        part_guid = uuid.UUID(f"53495830-2026-4000-8000-{idx:012d}")
        name_utf16 = name.encode("utf-16le")[:72]
        name_utf16 = name_utf16 + b"\x00" * (72 - len(name_utf16))

        entry = struct.pack(
            "<16s16sQQQ72s",
            guid_to_mixed_endian(tguid),
            guid_to_mixed_endian(part_guid),
            start_lba,
            end_lba,
            0,  # attributes
            name_utf16,
        )
        off = (idx - 1) * entry_size
        buf[off : off + entry_size] = entry

    return bytes(buf)


def build_gpt_header(
    my_lba: int,
    alternate_lba: int,
    first_usable_lba: int,
    last_usable_lba: int,
    part_entries_lba: int,
    entries_crc32: int,
) -> bytes:
    hdr_size = 92
    hdr_zero_crc = struct.pack(
        "<8sIIIIQQQQ16sQIII",
        b"EFI PART",
        0x00010000,         # Revision 1.0
        hdr_size,
        0,                  # Header CRC32 placeholder
        0,                  # Reserved
        my_lba,
        alternate_lba,
        first_usable_lba,
        last_usable_lba,
        guid_to_mixed_endian(DISK_GUID),
        part_entries_lba,
        128,                # Number of partition entries
        128,                # Size of each partition entry
        entries_crc32,
    )
    hdr_crc = zlib.crc32(hdr_zero_crc) & 0xFFFFFFFF
    hdr = (
        hdr_zero_crc[:16]
        + struct.pack("<I", hdr_crc)
        + hdr_zero_crc[20:]
    )
    sector = bytearray(SECTOR_SIZE)
    sector[: len(hdr)] = hdr
    return bytes(sector)


def write_gpt_tables(fd: int) -> None:
    os.ftruncate(fd, TOTAL_BYTES)

    pmbr = build_protective_mbr(TOTAL_SECTORS)
    entries = build_gpt_entries()
    entries_crc = zlib.crc32(entries) & 0xFFFFFFFF

    first_usable_lba = 34
    last_usable_lba = TOTAL_SECTORS - 34
    primary_entries_lba = 2
    backup_hdr_lba = TOTAL_SECTORS - 1
    backup_entries_lba = TOTAL_SECTORS - 33

    primary_hdr = build_gpt_header(
        my_lba=1,
        alternate_lba=backup_hdr_lba,
        first_usable_lba=first_usable_lba,
        last_usable_lba=last_usable_lba,
        part_entries_lba=primary_entries_lba,
        entries_crc32=entries_crc,
    )
    backup_hdr = build_gpt_header(
        my_lba=backup_hdr_lba,
        alternate_lba=1,
        first_usable_lba=first_usable_lba,
        last_usable_lba=last_usable_lba,
        part_entries_lba=backup_entries_lba,
        entries_crc32=entries_crc,
    )

    os.lseek(fd, 0, os.SEEK_SET)
    os.write(fd, pmbr)
    os.write(fd, primary_hdr)
    os.write(fd, entries)

    os.lseek(fd, backup_entries_lba * SECTOR_SIZE, os.SEEK_SET)
    os.write(fd, entries)
    os.write(fd, backup_hdr)


def ensure_disk_initialized(disk_path: str) -> int:
    os.makedirs(os.path.dirname(os.path.abspath(disk_path)), exist_ok=True)
    fd = os.open(disk_path, os.O_RDWR | os.O_CREAT, 0o644)
    write_gpt_tables(fd)
    return fd


def cmd_init(disk_path: str) -> int:
    fd = ensure_disk_initialized(disk_path)
    os.close(fd)
    return 0


def cmd_write_part(disk_path: str, part_name: str, src_path: str) -> int:
    if part_name not in PART_BY_NAME:
        print(f"mksingledisk: unknown partition '{part_name}'", file=sys.stderr)
        return 2
    _, start_mib, size_mib, _ = PART_BY_NAME[part_name]
    part_offset = start_mib * MIB
    part_bytes = size_mib * MIB

    fd = ensure_disk_initialized(disk_path)
    try:
        with open(src_path, "rb") as sf:
            os.lseek(fd, part_offset, os.SEEK_SET)
            written = 0
            while written < part_bytes:
                chunk = sf.read(min(65536, part_bytes - written))
                if not chunk:
                    break
                os.write(fd, chunk)
                written += len(chunk)
            # Zero-pad remainder of partition if src_path is smaller than part_bytes
            if written < part_bytes:
                zero_buf = b"\x00" * 65536
                rem = part_bytes - written
                while rem > 0:
                    step = min(len(zero_buf), rem)
                    os.write(fd, zero_buf[:step])
                    rem -= step
        write_gpt_tables(fd)
    finally:
        os.close(fd)
    return 0


def cmd_zero_part(disk_path: str, part_name: str) -> int:
    if part_name not in PART_BY_NAME:
        print(f"mksingledisk: unknown partition '{part_name}'", file=sys.stderr)
        return 2
    _, start_mib, size_mib, _ = PART_BY_NAME[part_name]
    part_offset = start_mib * MIB
    part_bytes = size_mib * MIB

    fd = ensure_disk_initialized(disk_path)
    try:
        os.lseek(fd, part_offset, os.SEEK_SET)
        zero_buf = b"\x00" * 65536
        rem = part_bytes
        while rem > 0:
            step = min(len(zero_buf), rem)
            os.write(fd, zero_buf[:step])
            rem -= step
        write_gpt_tables(fd)
    finally:
        os.close(fd)
    return 0


def cmd_has_part(disk_path: str, part_name: str) -> int:
    if part_name not in PART_BY_NAME:
        return 2
    if not os.path.exists(disk_path):
        return 1
    if os.path.getsize(disk_path) < TOTAL_BYTES:
        return 1
    _, start_mib, size_mib, _ = PART_BY_NAME[part_name]
    part_offset = start_mib * MIB
    with open(disk_path, "rb") as f:
        # Check first 8 KB (covers MBR/BPB at +0 and ext2/ext4/EROFS superblock at +1024)
        # and for ufs0 also checkLUN0 superblock at +1MiB+1024
        f.seek(part_offset)
        sample = f.read(8192)
        if any(b != 0 for b in sample):
            return 0
        if size_mib >= 5:
            f.seek(part_offset + MIB + 1024)
            sample2 = f.read(1024)
            if any(b != 0 for b in sample2):
                return 0
    return 1


def cmd_info(disk_path: str) -> int:
    print(f"Unified SIX GPT Disk Image: {disk_path} ({TOTAL_MIB} MiB = {TOTAL_BYTES} bytes = {TOTAL_SECTORS} sectors)")
    print(f"{'Part':<6} {'Name':<16} {'Start MiB':<10} {'Size MiB':<10} {'Start LBA':<12} {'End LBA':<12} {'Sectors':<10}")
    for idx, name, start_mib, size_mib, _ in PARTITIONS:
        start_lba = (start_mib * MIB) // SECTOR_SIZE
        sectors = (size_mib * MIB) // SECTOR_SIZE
        end_lba = start_lba + sectors - 1
        print(f"p{idx:<5} {name:<16} {start_mib:<10} {size_mib:<10} {start_lba:<12} {end_lba:<12} {sectors:<10}")
    return 0


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    subcmd = sys.argv[1]
    disk_path = sys.argv[2]

    if subcmd == "init":
        return cmd_init(disk_path)
    elif subcmd == "write-part" and len(sys.argv) == 5:
        return cmd_write_part(disk_path, sys.argv[3], sys.argv[4])
    elif subcmd == "zero-part" and len(sys.argv) == 4:
        return cmd_zero_part(disk_path, sys.argv[3])
    elif subcmd == "has-part" and len(sys.argv) == 4:
        return cmd_has_part(disk_path, sys.argv[3])
    elif subcmd == "info":
        return cmd_info(disk_path)
    else:
        print(__doc__.strip(), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
