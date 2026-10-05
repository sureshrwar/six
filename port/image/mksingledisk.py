#!/usr/bin/env python3
"""
mksingledisk.py -- Manage SIX's unified GPT-partitioned single host disk image
driven declaratively by port/image/disk_layout.json (Android Desktop / Aluminium
disk_layout architecture).

Usage:
  python3 port/image/mksingledisk.py [--layout PATH] [--type TYPE] [--adjust_part SPEC] init <disk_path>
  python3 port/image/mksingledisk.py [--layout PATH] [--type TYPE] write-part <disk_path> <part_name> <src_img>
  python3 port/image/mksingledisk.py [--layout PATH] [--type TYPE] zero-part <disk_path> <part_name>
  python3 port/image/mksingledisk.py [--layout PATH] [--type TYPE] has-part <disk_path> <part_name>
  python3 port/image/mksingledisk.py [--layout PATH] [--type TYPE] info [disk_path]
  python3 port/image/mksingledisk.py [--layout PATH] [--type TYPE] gen-header [header_path]
  python3 port/image/mksingledisk.py [--layout PATH] [--type TYPE] shell-vars
"""

import copy
import json
import os
import re
import struct
import sys
import uuid
import zlib
from typing import Any, Dict, List, Optional, Tuple, Union

SECTOR_SIZE = 512
KIB = 1024
MIB = 1024 * 1024
GIB = 1024 * 1024 * 1024

DEFAULT_LAYOUT_PATH = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "disk_layout.json"
)
DEFAULT_HEADER_PATH = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "include",
    "linux",
    "six_disk_layout.h",
)

# Standard GPT Partition Type GUIDs
GUID_LINUX_FS = uuid.UUID("0fc63daf-8483-4772-8e79-3d69d8477de4")
GUID_MS_BASIC_DATA = uuid.UUID("ebd0a0a2-b9e5-4433-87c0-68b6b72699c7")
GUID_EFI_SYSTEM = uuid.UUID("c12a7328-f81f-11d2-ba4b-00a0c93ec93b")

TYPE_GUID_MAP = {
    "data": GUID_LINUX_FS,
    "linux_fs": GUID_LINUX_FS,
    "rootfs": GUID_LINUX_FS,
    "kernel": GUID_LINUX_FS,
    "vbmeta": GUID_LINUX_FS,
    "firmware": GUID_LINUX_FS,
    "ms_basic_data": GUID_MS_BASIC_DATA,
    "ntfs": GUID_MS_BASIC_DATA,
    "efi": GUID_EFI_SYSTEM,
}


def parse_json_with_comments(path: str) -> Dict[str, Any]:
    """Parse a JSON file that allows '#' line comments (Android Desktop / ChromeOS style)."""
    cleaned_lines = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            stripped = line.lstrip()
            if stripped.startswith("#") or stripped.startswith("//"):
                cleaned_lines.append("")
            else:
                cleaned_lines.append(line)
    return json.loads("\n".join(cleaned_lines))


def parse_human_size(operand: Union[str, int]) -> int:
    """Parse human-friendly size strings like '50 MiB', '4 GiB', '512 KiB' into bytes."""
    if isinstance(operand, int):
        return operand
    s = str(operand).strip()
    neg = -1 if s.startswith("-") else 1
    if neg == -1 or s.startswith("+"):
        s = s[1:].strip()
    m = re.match(r"^(\d+)\s*([A-Za-z]*)$", s)
    if not m:
        raise ValueError(f"Invalid size specification: {operand!r}")
    val = int(m.group(1))
    unit = m.group(2).upper()
    factors = {
        "": 1,
        "B": 1,
        "K": KIB,
        "KB": 1000,
        "KIB": KIB,
        "M": MIB,
        "MB": 1000 * 1000,
        "MIB": MIB,
        "G": GIB,
        "GB": 1000 * 1000 * 1000,
        "GIB": GIB,
    }
    if unit not in factors:
        raise ValueError(f"Unknown size unit {unit!r} in {operand!r}")
    return val * factors[unit] * neg


def label_to_macro_stem(label: str) -> str:
    return re.sub(r"[^A-Za-z0-9]+", "_", label).upper().strip("_")


class DiskLayoutConfig:
    """Represents a resolved GPT disk layout loaded from disk_layout.json."""

    def __init__(
        self,
        layout_path: str = DEFAULT_LAYOUT_PATH,
        layout_type: str = "base",
        adjustments: Optional[List[str]] = None,
    ):
        self.layout_path = layout_path
        self.layout_type = layout_type
        raw = parse_json_with_comments(layout_path)

        meta = raw.get("metadata", {})
        self.sector_size = int(meta.get("block_size", SECTOR_SIZE))
        self.fs_block_size = int(meta.get("fs_block_size", KIB))
        self.fs_align = parse_human_size(meta.get("fs_align", "1 MiB"))
        self.min_unified_bytes = parse_human_size(meta.get("min_unified_size", "180 MiB"))
        self.disk_guid = uuid.UUID(
            meta.get("disk_guid", "53495830-2026-4000-8000-000000000000")
        )
        self.part_guid_prefix = meta.get(
            "part_guid_prefix", "53495830-2026-4000-8000"
        )

        layouts = raw.get("layouts", {})
        if "common" not in layouts:
            raise ValueError(f"Missing 'common' layout in {layout_path}")

        merged_parts = copy.deepcopy(layouts["common"])
        if layout_type != "common" and layout_type in layouts:
            overrides_by_num = {
                int(p["num"]): p for p in layouts[layout_type] if "num" in p
            }
            overrides_by_label = {
                p["label"]: p
                for p in layouts[layout_type]
                if "label" in p and "num" not in p
            }
            for part in merged_parts:
                pnum = int(part["num"])
                plabel = part["label"]
                if pnum in overrides_by_num:
                    part.update(overrides_by_num[pnum])
                elif plabel in overrides_by_label:
                    part.update(overrides_by_label[plabel])

        # Apply optional --adjust_part <label>:<op><size> adjustments
        if adjustments:
            for spec in adjustments:
                m = re.match(r"^([^:]+):([+=-]?)(.+)$", spec.strip())
                if not m:
                    raise ValueError(f"Invalid --adjust_part spec: {spec!r}")
                target_label, op, sz_str = m.group(1), m.group(2), m.group(3)
                delta = parse_human_size(sz_str)
                found = False
                for part in merged_parts:
                    if part["label"] == target_label:
                        cur_bytes = parse_human_size(part["size"])
                        if op == "+":
                            new_bytes = cur_bytes + delta
                        elif op == "-":
                            new_bytes = cur_bytes - abs(delta)
                        else:
                            new_bytes = abs(delta)
                        part["size"] = f"{new_bytes // MIB} MiB" if (new_bytes % MIB == 0) else str(new_bytes)
                        found = True
                        break
                if not found:
                    raise KeyError(f"Partition '{target_label}' not found for adjustment {spec!r}")

        # Compute cumulative aligned offsets for all partitions
        cur_offset = self.fs_align
        self.partitions: List[Dict[str, Any]] = []
        self.part_tuples: List[Tuple[int, str, int, int, uuid.UUID]] = []
        self.part_by_name: Dict[str, Tuple[int, int, int, uuid.UUID]] = {}
        self.part_meta_by_name: Dict[str, Dict[str, Any]] = {}

        for p in merged_parts:
            if cur_offset % self.fs_align != 0:
                cur_offset = ((cur_offset + self.fs_align - 1) // self.fs_align) * self.fs_align
            size_bytes = parse_human_size(p.get("size", "0 B"))
            if size_bytes <= 0:
                continue
            idx = int(p["num"])
            name = str(p["label"])
            c_macro = str(p.get("c_macro", label_to_macro_stem(name)))
            ptype_str = str(p.get("type", "data")).lower()
            tguid = TYPE_GUID_MAP.get(ptype_str, GUID_LINUX_FS)

            start_mib = cur_offset // MIB
            size_mib = size_bytes // MIB
            start_lba = cur_offset // self.sector_size
            sectors = size_bytes // self.sector_size
            end_lba = start_lba + sectors - 1

            resolved = dict(p)
            resolved.update(
                {
                    "num": idx,
                    "label": name,
                    "c_macro": c_macro,
                    "type_guid": tguid,
                    "start_bytes": cur_offset,
                    "size_bytes": size_bytes,
                    "start_mib": start_mib,
                    "size_mib": size_mib,
                    "start_lba": start_lba,
                    "end_lba": end_lba,
                    "sectors": sectors,
                    "blocks_1k": size_bytes // KIB,
                }
            )
            self.partitions.append(resolved)
            self.part_tuples.append((idx, name, start_mib, size_mib, tguid))
            self.part_by_name[name] = (idx, start_mib, size_mib, tguid)
            self.part_meta_by_name[name] = resolved
            cur_offset += size_bytes

        # Reserve trailing fs_align (1 MiB) for backup GPT entries + backup GPT header
        total_bytes = cur_offset + self.fs_align
        if total_bytes % self.fs_align != 0:
            total_bytes = ((total_bytes + self.fs_align - 1) // self.fs_align) * self.fs_align

        self.total_bytes = total_bytes
        self.total_mib = total_bytes // MIB
        self.total_sectors = total_bytes // self.sector_size


# Default module-level layout instance and backwards-compatible constants
DEFAULT_LAYOUT = DiskLayoutConfig()
DISK_GUID = DEFAULT_LAYOUT.disk_guid
TOTAL_MIB = DEFAULT_LAYOUT.total_mib
TOTAL_BYTES = DEFAULT_LAYOUT.total_bytes
TOTAL_SECTORS = DEFAULT_LAYOUT.total_sectors
PARTITIONS = DEFAULT_LAYOUT.part_tuples
PART_BY_NAME = DEFAULT_LAYOUT.part_by_name


def set_active_layout(layout: DiskLayoutConfig) -> None:
    global DEFAULT_LAYOUT, DISK_GUID, TOTAL_MIB, TOTAL_BYTES, TOTAL_SECTORS, PARTITIONS, PART_BY_NAME
    DEFAULT_LAYOUT = layout
    DISK_GUID = layout.disk_guid
    TOTAL_MIB = layout.total_mib
    TOTAL_BYTES = layout.total_bytes
    TOTAL_SECTORS = layout.total_sectors
    PARTITIONS = layout.part_tuples
    PART_BY_NAME = layout.part_by_name


def guid_to_mixed_endian(u: uuid.UUID) -> bytes:
    return u.bytes_le


def mixed_endian_to_guid(b: bytes) -> uuid.UUID:
    return uuid.UUID(bytes_le=b[:16])


def read_gpt_from_disk(disk_path: str) -> Optional[Dict[str, Dict[str, Any]]]:
    """Read live UEFI GPT partition entries directly from a disk image if valid."""
    if not disk_path or not os.path.exists(disk_path):
        return None
    try:
        with open(disk_path, "rb") as f:
            f.seek(SECTOR_SIZE)
            hdr = f.read(92)
            if len(hdr) < 92 or hdr[:8] != b"EFI PART":
                return None
            (
                _sig,
                _rev,
                _hsize,
                _hcrc,
                _rsvd,
                _my_lba,
                _alt_lba,
                _first_lba,
                _last_lba,
                _dguid,
                entries_lba,
                num_entries,
                entry_size,
                _ecrc,
            ) = struct.unpack("<8sIIIIQQQQ16sQIII", hdr)
            if entry_size < 128 or num_entries <= 0 or num_entries > 256:
                return None
            f.seek(entries_lba * SECTOR_SIZE)
            raw_entries = f.read(num_entries * entry_size)
            if len(raw_entries) < num_entries * entry_size:
                return None
            parts: Dict[str, Dict[str, Any]] = {}
            for i in range(num_entries):
                ent = raw_entries[i * entry_size : i * entry_size + 128]
                tguid_b, pguid_b, start_lba, end_lba, attrs, name_u16 = struct.unpack(
                    "<16s16sQQQ72s", ent
                )
                if tguid_b == b"\x00" * 16 or start_lba == 0 or end_lba < start_lba:
                    continue
                name = name_u16.decode("utf-16le", errors="ignore").split("\x00", 1)[0]
                if not name:
                    continue
                sectors = end_lba - start_lba + 1
                parts[name] = {
                    "num": i + 1,
                    "label": name,
                    "start_lba": start_lba,
                    "end_lba": end_lba,
                    "sectors": sectors,
                    "start_bytes": start_lba * SECTOR_SIZE,
                    "size_bytes": sectors * SECTOR_SIZE,
                    "type_guid": mixed_endian_to_guid(tguid_b),
                    "part_guid": mixed_endian_to_guid(pguid_b),
                    "attrs": attrs,
                }
            return parts if parts else None
    except OSError:
        return None


def get_partition_offset_bytes(
    part_name: str,
    disk_path: Optional[str] = None,
    layout: Optional[DiskLayoutConfig] = None,
) -> int:
    """Resolve a partition's byte offset from live disk GPT if available, else disk_layout.json."""
    if disk_path:
        gpt = read_gpt_from_disk(disk_path)
        if gpt and part_name in gpt:
            return int(gpt[part_name]["start_bytes"])
    cfg = layout or DEFAULT_LAYOUT
    if part_name not in cfg.part_meta_by_name:
        raise KeyError(f"Unknown partition {part_name!r} in disk_layout.json")
    return int(cfg.part_meta_by_name[part_name]["start_bytes"])


def get_partition_size_bytes(
    part_name: str,
    disk_path: Optional[str] = None,
    layout: Optional[DiskLayoutConfig] = None,
) -> int:
    """Resolve a partition's size in bytes from live disk GPT if available, else disk_layout.json."""
    if disk_path:
        gpt = read_gpt_from_disk(disk_path)
        if gpt and part_name in gpt:
            return int(gpt[part_name]["size_bytes"])
    cfg = layout or DEFAULT_LAYOUT
    if part_name not in cfg.part_meta_by_name:
        raise KeyError(f"Unknown partition {part_name!r} in disk_layout.json")
    return int(cfg.part_meta_by_name[part_name]["size_bytes"])


def build_protective_mbr(total_sectors: int) -> bytes:
    mbr = bytearray(SECTOR_SIZE)
    start_lba = 1
    num_sectors = min(total_sectors - 1, 0xFFFFFFFF)
    mbr[446 : 446 + 16] = struct.pack(
        "<BBBBBBBBII",
        0x00,
        0x00,
        0x02,
        0x00,
        0xEE,
        0xFE,
        0xFF,
        0xFF,
        start_lba,
        num_sectors,
    )
    mbr[510:512] = b"\x55\xaa"
    return bytes(mbr)


def build_gpt_entries() -> bytes:
    num_entries = 128
    entry_size = 128
    buf = bytearray(num_entries * entry_size)

    for p in DEFAULT_LAYOUT.partitions:
        idx = p["num"]
        name = p["label"]
        start_lba = p["start_lba"]
        end_lba = p["end_lba"]
        tguid = p["type_guid"]
        part_guid = uuid.UUID(f"{DEFAULT_LAYOUT.part_guid_prefix}-{idx:012d}")
        name_utf16 = name.encode("utf-16le")[:72]
        name_utf16 = name_utf16 + b"\x00" * (72 - len(name_utf16))

        entry = struct.pack(
            "<16s16sQQQ72s",
            guid_to_mixed_endian(tguid),
            guid_to_mixed_endian(part_guid),
            start_lba,
            end_lba,
            0,
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
        0x00010000,
        hdr_size,
        0,
        0,
        my_lba,
        alternate_lba,
        first_usable_lba,
        last_usable_lba,
        guid_to_mixed_endian(DISK_GUID),
        part_entries_lba,
        128,
        128,
        entries_crc32,
    )
    hdr_crc = zlib.crc32(hdr_zero_crc) & 0xFFFFFFFF
    hdr = hdr_zero_crc[:16] + struct.pack("<I", hdr_crc) + hdr_zero_crc[20:]
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
    pmeta = DEFAULT_LAYOUT.part_meta_by_name[part_name]
    part_offset = pmeta["start_bytes"]
    part_bytes = pmeta["size_bytes"]

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
    pmeta = DEFAULT_LAYOUT.part_meta_by_name[part_name]
    part_offset = pmeta["start_bytes"]
    part_bytes = pmeta["size_bytes"]

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
    pmeta = DEFAULT_LAYOUT.part_meta_by_name[part_name]
    part_offset = pmeta["start_bytes"]
    size_mib = pmeta["size_mib"]
    with open(disk_path, "rb") as f:
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


def generate_c_header(layout: DiskLayoutConfig) -> str:
    """Render include/linux/six_disk_layout.h from the declarative disk_layout.json."""
    lines = [
        "/*",
        " * include/linux/six_disk_layout.h",
        " *",
        " * AUTO-GENERATED by port/image/mksingledisk.py from port/image/disk_layout.json.",
        " * DO NOT EDIT MANUALLY -- edit port/image/disk_layout.json instead.",
        " *",
        f" * Unified Single Multi-Partition Host Disk Image Layout for SIX ({layout.total_mib} MiB):",
        " *   LBA 0           (0 .. 511):          Protective MBR (0xEE)",
        " *   LBA 1           (512 .. 1023):       Primary GPT Header (\"EFI PART\")",
        f" *   LBA 2..33       (1024 .. 17407):     Primary GPT Partition Entries (1..{len(layout.partitions)})",
    ]
    for p in layout.partitions:
        s_mib = p["start_mib"]
        e_mib = s_mib + p["size_mib"]
        sz_mib = p["size_mib"]
        dev = p.get("device", "")
        lines.append(
            f" *   Partition {p['num']:<2}    ({s_mib:>3} MiB .. {e_mib:>3} MiB, {sz_mib:>3} MiB): "
            f"{p['label']:<14} ({dev})"
        )
    footer_start = layout.total_mib - (layout.fs_align // MIB)
    lines.extend(
        [
            f" *   Footer          ({footer_start:>3} MiB .. {layout.total_mib:>3} MiB,   1 MiB): Backup GPT Entries & Header",
            " */",
            "#ifndef _LINUX_SIX_DISK_LAYOUT_H",
            "#define _LINUX_SIX_DISK_LAYOUT_H",
            "",
            "#define SIX_MIB_BYTES\t\t\t(1024UL * 1024UL)",
            "#define SIX_MIB_SECTORS\t\t\t(2048UL)",
            "",
            f"#define SIX_SINGLE_DISK_TOTAL_MIB\t{layout.total_mib}UL",
            "#define SIX_SINGLE_DISK_TOTAL_BYTES\t(SIX_SINGLE_DISK_TOTAL_MIB * SIX_MIB_BYTES)",
            "#define SIX_SINGLE_DISK_TOTAL_SECTORS\t(SIX_SINGLE_DISK_TOTAL_MIB * SIX_MIB_SECTORS)",
            "",
            f"/* Any backing file >= {layout.min_unified_bytes // MIB} MiB is treated as a unified multi-partition image */",
            f"#define SIX_SINGLE_DISK_MIN_BYTES\t({layout.min_unified_bytes // MIB}UL * SIX_MIB_BYTES)",
            "",
            f"#define SIX_GPT_MAX_PARTITIONS\t\t{max(16, len(layout.partitions) + 4)}",
            "",
        ]
    )

    for p in layout.partitions:
        macro = p["c_macro"]
        s_mib = p["start_mib"]
        sz_mib = p["size_mib"]
        dev = p.get("device", "")
        lines.append(
            f"/* Partition {p['num']}: {p['label']} ({dev}) - {sz_mib} MiB at {s_mib} MiB */"
        )
        lines.append(f"#define SIX_PART_{macro}_NUM\t\t{p['num']}")
        lines.append(f"#define SIX_PART_{macro}_LABEL\t\t\"{p['label']}\"")
        lines.append(f"#define SIX_PART_{macro}_OFFSET\t\t({s_mib}UL * SIX_MIB_BYTES)")
        lines.append(f"#define SIX_PART_{macro}_BYTES\t\t({sz_mib}UL * SIX_MIB_BYTES)")
        lines.append(f"#define SIX_PART_{macro}_SECTORS\t({sz_mib}UL * SIX_MIB_SECTORS)")
        lines.append("")

    lines.extend(
        [
            "struct six_gpt_part_info {",
            "\tint part_num;",
            "\tchar label[36];",
            "\tunsigned long start_lba;",
            "\tunsigned long end_lba;",
            "\tunsigned long sectors;",
            "\tunsigned long offset_bytes;",
            "\tunsigned long size_bytes;",
            "};",
            "",
            "extern char six_root_disk_path[256];",
            "extern unsigned long six_disk_offset[4];",
            "extern int six_gpt_lookup_part(const char *label, unsigned long *offset_out,",
            "\t\t\t       unsigned long *bytes_out, int *part_num_out);",
            "extern int six_gpt_get_part_count(void);",
            "extern const struct six_gpt_part_info *six_gpt_get_part_by_index(int idx);",
            "",
            "#endif /* _LINUX_SIX_DISK_LAYOUT_H */",
            "",
        ]
    )
    return "\n".join(lines)


def cmd_gen_header(header_path: str) -> int:
    content = generate_c_header(DEFAULT_LAYOUT)
    old_content = None
    if os.path.exists(header_path):
        with open(header_path, "r", encoding="utf-8") as f:
            old_content = f.read()
    if old_content != content:
        os.makedirs(os.path.dirname(os.path.abspath(header_path)), exist_ok=True)
        with open(header_path, "w", encoding="utf-8") as f:
            f.write(content)
        print(
            f"mksingledisk: generated {header_path} from {os.path.relpath(DEFAULT_LAYOUT.layout_path)} "
            f"({len(DEFAULT_LAYOUT.partitions)} GPT partitions, {DEFAULT_LAYOUT.total_mib} MiB)"
        )
    return 0


def cmd_shell_vars() -> int:
    print(f"# Auto-generated from {DEFAULT_LAYOUT.layout_path} (layout={DEFAULT_LAYOUT.layout_type})")
    print(f"SIX_DISK_SECTOR_SIZE={DEFAULT_LAYOUT.sector_size}")
    print(f"SIX_DISK_TOTAL_MIB={DEFAULT_LAYOUT.total_mib}")
    print(f"SIX_DISK_TOTAL_BYTES={DEFAULT_LAYOUT.total_bytes}")
    print(f"SIX_DISK_TOTAL_SECTORS={DEFAULT_LAYOUT.total_sectors}")
    print(f"SIX_DISK_NUM_PARTITIONS={len(DEFAULT_LAYOUT.partitions)}")
    for p in DEFAULT_LAYOUT.partitions:
        m = p["c_macro"]
        print(f"PARTITION_NUM_{m}={p['num']}")
        print(f"SIX_PART_LABEL_{m}=\"{p['label']}\"")
        print(f"SIX_PART_START_MIB_{m}={p['start_mib']}")
        print(f"SIX_PART_SIZE_MIB_{m}={p['size_mib']}")
        print(f"SIX_PART_OFFSET_BYTES_{m}={p['start_bytes']}")
        print(f"SIX_PART_SIZE_BYTES_{m}={p['size_bytes']}")
        print(f"SIX_PART_START_LBA_{m}={p['start_lba']}")
        print(f"SIX_PART_END_LBA_{m}={p['end_lba']}")
        print(f"SIX_PART_SECTORS_{m}={p['sectors']}")
        print(f"SIX_PART_BLOCKS_1K_{m}={p['blocks_1k']}")
        if "subpartitions" in p:
            for sp in p["subpartitions"]:
                sm = label_to_macro_stem(sp["label"])
                sp_bytes = parse_human_size(sp["size"])
                sp_data = parse_human_size(sp.get("data_size", sp["size"]))
                print(f"SIX_SUBPART_SIZE_BYTES_{sm}={sp_bytes}")
                print(f"SIX_SUBPART_DATA_BLOCKS_1K_{sm}={sp_data // KIB}")
                print(f"SIX_SUBPART_TOTAL_BLOCKS_1K_{sm}={sp_bytes // KIB}")
    return 0


def cmd_info(disk_path: str) -> int:
    print(
        f"Unified SIX GPT Disk Image: {disk_path} "
        f"(layout={os.path.relpath(DEFAULT_LAYOUT.layout_path)} [{DEFAULT_LAYOUT.layout_type}], "
        f"{TOTAL_MIB} MiB = {TOTAL_BYTES} bytes = {TOTAL_SECTORS} sectors)"
    )
    print(
        f"{'Part':<6} {'Name':<16} {'Start MiB':<10} {'Size MiB':<10} "
        f"{'Start LBA':<12} {'End LBA':<12} {'Sectors':<10} {'FS/Type':<10}"
    )
    for p in DEFAULT_LAYOUT.partitions:
        idx = p["num"]
        name = p["label"]
        start_mib = p["start_mib"]
        size_mib = p["size_mib"]
        start_lba = p["start_lba"]
        end_lba = p["end_lba"]
        sectors = p["sectors"]
        fst = p.get("fs_format", p.get("type", "data"))
        print(
            f"p{idx:<5} {name:<16} {start_mib:<10} {size_mib:<10} "
            f"{start_lba:<12} {end_lba:<12} {sectors:<10} {fst:<10}"
        )
    return 0


def main() -> int:
    argv = sys.argv[1:]
    layout_path = os.environ.get("SIX_DISK_LAYOUT", DEFAULT_LAYOUT_PATH)
    layout_type = os.environ.get("SIX_DISK_LAYOUT_TYPE", "base")
    adjustments: List[str] = []

    while argv and argv[0].startswith("--"):
        opt = argv.pop(0)
        if opt == "--layout" and argv:
            layout_path = argv.pop(0)
        elif opt.startswith("--layout="):
            layout_path = opt.split("=", 1)[1]
        elif opt == "--type" and argv:
            layout_type = argv.pop(0)
        elif opt.startswith("--type="):
            layout_type = opt.split("=", 1)[1]
        elif opt == "--adjust_part" and argv:
            adjustments.append(argv.pop(0))
        elif opt.startswith("--adjust_part="):
            adjustments.append(opt.split("=", 1)[1])
        else:
            print(__doc__.strip(), file=sys.stderr)
            return 2

    if (
        layout_path != DEFAULT_LAYOUT_PATH
        or layout_type != "base"
        or adjustments
    ):
        set_active_layout(DiskLayoutConfig(layout_path, layout_type, adjustments))

    if not argv:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    subcmd = argv[0]

    if subcmd == "gen-header":
        hdr_out = argv[1] if len(argv) >= 2 else DEFAULT_HEADER_PATH
        return cmd_gen_header(hdr_out)
    elif subcmd == "shell-vars":
        return cmd_shell_vars()
    elif subcmd == "info":
        disk_path = argv[1] if len(argv) >= 2 else "disk/x86/root"
        return cmd_info(disk_path)

    if len(argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    disk_path = argv[1]

    if subcmd == "init":
        return cmd_init(disk_path)
    elif subcmd == "write-part" and len(argv) == 4:
        return cmd_write_part(disk_path, argv[2], argv[3])
    elif subcmd == "zero-part" and len(argv) == 3:
        return cmd_zero_part(disk_path, argv[2])
    elif subcmd == "has-part" and len(argv) == 3:
        return cmd_has_part(disk_path, argv[2])
    else:
        print(__doc__.strip(), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
