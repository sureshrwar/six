#!/usr/bin/env python3
"""
mkerofs.py -- Build a Linux-compatible EROFS v1 filesystem image (magic 0xE0F5E1E2)
for SIX without requiring host erofs-utils or root/fakeroot privileges.

Layout (1024-byte blocks, blkszbits=10):
  Block 0 (0..1023):       Boot/padding sector (zeros)
  Block 1..M (meta area):  meta_blkaddr = 1
    NID 0..3 (offset 1024..1151): 128-byte struct erofs_super_block
    NID 4..N (offset 1152..):     32-byte struct erofs_inode_compact slots
                                  (+ inline tail data for EROFS_INODE_FLAT_INLINE)
  Block M+1..end:          Contiguous 1024-byte data blocks for directories and
                           regular files (EROFS_INODE_FLAT_PLAIN).
"""

import argparse
import hashlib
import os
import stat
import struct
import sys
import time
import uuid as uuid_mod

EROFS_SUPER_MAGIC_V1 = 0xE0F5E1E2
BLOCK_SIZE = 1024
BLKSZBITS = 10
SLOT_SIZE = 32

EROFS_FEATURE_INCOMPAT_VERITY = 0x00000200
EROFS_VERITY_EXTSLOTS = 5
VERITY_SALT_STR = "SIX_EROFS_VERITY_SALT_2026"
VERITY_SALT = VERITY_SALT_STR.encode("ascii").ljust(32, b"\x00")
VERITY_SALT_PADDED = VERITY_SALT + b"\x00" * 32


def erofs_verity_hash_1k(blk: bytes) -> bytes:
    assert len(blk) == BLOCK_SIZE
    return hashlib.sha256(VERITY_SALT_PADDED + blk).digest()


EROFS_I_VERS_COMPACT = 0
EROFS_INODE_FLAT_PLAIN = 0
EROFS_INODE_FLAT_INLINE = 2

EROFS_FT_UNKNOWN = 0
EROFS_FT_REG_FILE = 1
EROFS_FT_DIR = 2
EROFS_FT_CHRDEV = 3
EROFS_FT_BLKDEV = 4
EROFS_FT_FIFO = 5
EROFS_FT_SOCK = 6
EROFS_FT_SYMLINK = 7


class Node:
    def __init__(self, path: str, mode: int, uid: int = 0, gid: int = 0):
        self.path = path
        self.name = os.path.basename(path) if path != "/" else ""
        self.mode = mode
        self.uid = uid
        self.gid = gid
        self.rdev = 0
        self.symlink_target = b""
        self.data = b""
        self.children = {}  # name -> Node
        self.parent = None
        self.nid = 0
        self.ino = 0
        self.datalayout = EROFS_INODE_FLAT_PLAIN
        self.raw_blkaddr = 0
        self.inline_data = b""
        self.size = 0

    def is_dir(self) -> bool:
        return stat.S_ISDIR(self.mode)

    def is_reg(self) -> bool:
        return stat.S_ISREG(self.mode)

    def is_lnk(self) -> bool:
        return stat.S_ISLNK(self.mode)

    def is_chr(self) -> bool:
        return stat.S_ISCHR(self.mode)

    def is_blk(self) -> bool:
        return stat.S_ISBLK(self.mode)

    def file_type(self) -> int:
        if self.is_reg():
            return EROFS_FT_REG_FILE
        if self.is_dir():
            return EROFS_FT_DIR
        if self.is_lnk():
            return EROFS_FT_SYMLINK
        if self.is_chr():
            return EROFS_FT_CHRDEV
        if self.is_blk():
            return EROFS_FT_BLKDEV
        if stat.S_ISFIFO(self.mode):
            return EROFS_FT_FIFO
        if stat.S_ISSOCK(self.mode):
            return EROFS_FT_SOCK
        return EROFS_FT_UNKNOWN


def ensure_parent_dirs(root: Node, rel_path: str) -> Node:
    parts = [p for p in rel_path.strip("/").split("/") if p]
    cur = root
    built = ""
    for p in parts[:-1]:
        built = built + "/" + p
        if p not in cur.children:
            d = Node(built, stat.S_IFDIR | 0o755)
            d.parent = cur
            cur.children[p] = d
        cur = cur.children[p]
    return cur


def load_from_stage(stage_dir: str) -> Node:
    root = Node("/", stat.S_IFDIR | 0o755)
    root.parent = root

    for dirpath, dirnames, filenames in os.walk(stage_dir, followlinks=False):
        rel_dir = os.path.relpath(dirpath, stage_dir)
        if rel_dir == ".":
            cur_dir = root
            cur_path = ""
        else:
            cur_path = "/" + rel_dir.replace(os.sep, "/")
            parent = ensure_parent_dirs(root, cur_path)
            bname = os.path.basename(cur_path)
            if bname not in parent.children:
                st = os.lstat(dirpath)
                d = Node(cur_path, stat.S_IFDIR | (st.st_mode & 0o7777))
                d.parent = parent
                parent.children[bname] = d
            cur_dir = parent.children[bname]

        for dname in sorted(dirnames):
            full = os.path.join(dirpath, dname)
            st = os.lstat(full)
            child_path = (cur_path + "/" + dname) if cur_path else ("/" + dname)
            if stat.S_ISLNK(st.st_mode):
                n = Node(child_path, stat.S_IFLNK | 0o777)
                n.symlink_target = os.readlink(full).encode("utf-8")
                n.size = len(n.symlink_target)
            else:
                n = Node(child_path, stat.S_IFDIR | (st.st_mode & 0o7777))
            n.parent = cur_dir
            cur_dir.children[dname] = n

        for fname in sorted(filenames):
            full = os.path.join(dirpath, fname)
            st = os.lstat(full)
            child_path = (cur_path + "/" + fname) if cur_path else ("/" + fname)
            if stat.S_ISLNK(st.st_mode):
                n = Node(child_path, stat.S_IFLNK | 0o777)
                n.symlink_target = os.readlink(full).encode("utf-8")
                n.size = len(n.symlink_target)
            elif stat.S_ISREG(st.st_mode):
                n = Node(child_path, stat.S_IFREG | (st.st_mode & 0o7777))
                with open(full, "rb") as f:
                    n.data = f.read()
                n.size = len(n.data)
            else:
                continue
            n.parent = cur_dir
            cur_dir.children[fname] = n

    return root


def apply_manifest_metadata(root: Node, manifest_path: str, mode: str) -> None:
    if not manifest_path or not os.path.exists(manifest_path):
        return

    with open(manifest_path, "r", encoding="utf-8") as f:
        for raw_line in f:
            line = raw_line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) < 3:
                continue
            etype, path, perm_str = parts[0], parts[1], parts[2]
            perm = 0o777 if perm_str == "-" else int(perm_str, 8)

            if mode == "bin":
                if path != "/bin" and not path.startswith("/bin/"):
                    continue
                if path == "/bin":
                    root.mode = stat.S_IFDIR | perm
                    continue
                path = path[4:]  # strip leading "/bin"
            elif mode == "root":
                if path.startswith("/bin/"):
                    continue

            if path == "/":
                root.mode = stat.S_IFDIR | perm
                continue

            parent = ensure_parent_dirs(root, path)
            bname = os.path.basename(path)
            if etype == "dir":
                if bname in parent.children:
                    parent.children[bname].mode = stat.S_IFDIR | perm
                else:
                    d = Node(path, stat.S_IFDIR | perm)
                    d.parent = parent
                    parent.children[bname] = d
            elif etype == "file":
                if bname in parent.children:
                    parent.children[bname].mode = stat.S_IFREG | perm
            elif etype == "sym" and len(parts) >= 4:
                target = parts[3].encode("utf-8")
                s = Node(path, stat.S_IFLNK | perm)
                s.symlink_target = target
                s.size = len(target)
                s.parent = parent
                parent.children[bname] = s
            elif etype == "node" and len(parts) >= 6:
                ntype, maj, min_nr = parts[3], int(parts[4]), int(parts[5])
                fmode = stat.S_IFCHR if ntype == "c" else stat.S_IFBLK
                dev_node = Node(path, fmode | perm)
                dev_node.rdev = (maj << 8) | min_nr
                dev_node.parent = parent
                parent.children[bname] = dev_node


def collect_nodes(root: Node) -> list:
    nodes = []

    def walk(n: Node):
        nodes.append(n)
        if n.is_dir():
            for k in sorted(n.children.keys()):
                walk(n.children[k])

    walk(root)
    return nodes


def build_dir_data(dnode: Node) -> bytes:
    entries = [
        (b".", dnode.nid, EROFS_FT_DIR),
        (b"..", dnode.parent.nid if dnode.parent else dnode.nid, EROFS_FT_DIR),
    ]
    for name in sorted(dnode.children.keys()):
        child = dnode.children[name]
        entries.append((name.encode("utf-8"), child.nid, child.file_type()))

    # Sort all entries in the directory block lexicographically by name
    entries.sort(key=lambda e: e[0])

    blocks = []
    idx = 0
    n = len(entries)
    while idx < n:
        batch = []
        names_len = 0
        while idx < n:
            name_b = entries[idx][0]
            needed = (len(batch) + 1) * 12 + names_len + len(name_b)
            if needed > BLOCK_SIZE and batch:
                break
            batch.append(entries[idx])
            names_len += len(name_b)
            idx += 1

        blk = bytearray(BLOCK_SIZE)
        nameoff = len(batch) * 12
        for i, (name_b, nid, ft) in enumerate(batch):
            struct.pack_into("<QHBB", blk, i * 12, nid, nameoff, ft, 0)
            blk[nameoff : nameoff + len(name_b)] = name_b
            nameoff += len(name_b)
        blocks.append(bytes(blk))

    return b"".join(blocks)


def main() -> int:
    ap = argparse.ArgumentParser(description="Build an EROFS v1 filesystem image")
    ap.add_argument("--stage", required=True, help="Staging directory path")
    ap.add_argument("--manifest", default="", help="Optional manifest.txt path")
    ap.add_argument("--mode", default="bin", help="Manifest mode (bin|root|full)")
    ap.add_argument("--label", default="bin_verity", help="Volume label (up to 16 chars)")
    ap.add_argument("--uuid", default="13bcf00c-78b2-11d9-8fdf-f213c4292cfb", help="Volume UUID")
    ap.add_argument("--blocks", type=int, default=0, help="Total 1KB blocks to pad image to (0 = exact fit)")
    ap.add_argument("--verity", action="store_true", help="Enable Native EROFS Verity (go/erofs-verity)")
    ap.add_argument("--digest-out", default="", help="Optional file path to write 64-hex superblock root_digest")
    ap.add_argument("--out", required=True, help="Output image file path")
    args = ap.parse_args()

    root = load_from_stage(args.stage)
    if args.manifest:
        apply_manifest_metadata(root, args.manifest, args.mode)

    all_nodes = collect_nodes(root)

    # Pre-compute directory sizes (independent of child NIDs) so we know how many
    # data blocks each directory needs when reserving inline digest slots.
    for node in all_nodes:
        if node.is_dir():
            node.size = len(build_dir_data(node))

    # Assign NIDs:
    #   - Stock EROFS: starts at NID 4 (NIDs 0..3 hold the 128-byte superblock).
    #   - Native EROFS Verity (go/erofs-verity): Block 1 (NIDs 0..31) holds the
    #     128-byte superblock + 80-byte struct erofs_verity_sb_ext (5 extslots),
    #     authenticated by root_digest = SHA256(Block 1). Inodes and inline
    #     digest tables start cleanly at Block 2 (NID 32, meta_verity_startblk=2).
    cur_nid = 32 if args.verity else 4
    for i, node in enumerate(all_nodes, start=1):
        node.ino = i
        node.nid = cur_nid
        if node.is_lnk():
            # Store symlink target inline using EROFS_INODE_FLAT_INLINE
            node.datalayout = EROFS_INODE_FLAT_INLINE
            node.inline_data = node.symlink_target
            inline_slots = (len(node.inline_data) + SLOT_SIZE - 1) // SLOT_SIZE
            cur_nid += 1 + inline_slots
        else:
            node.datalayout = EROFS_INODE_FLAT_PLAIN
            if args.verity and (node.is_dir() or node.is_reg()) and node.size > 0:
                nblks = (node.size + BLOCK_SIZE - 1) // BLOCK_SIZE
                cur_nid += 1 + nblks  # 1 inode slot + nblks 32-byte inline digest slots
            else:
                cur_nid += 1

    # Now that every node has its final NID, serialize directory data blocks
    for node in all_nodes:
        if node.is_dir():
            node.data = build_dir_data(node)
            node.size = len(node.data)

    # Calculate how many 1024-byte blocks the metadata area (starting at block 1) needs
    meta_bytes = cur_nid * SLOT_SIZE
    meta_blocks = (meta_bytes + BLOCK_SIZE - 1) // BLOCK_SIZE

    if args.verity:
        meta_verity_startblk = 2
        meta_verity_blocks = max(0, meta_blocks - 1)
        meta_merkle_blkaddr = 1 + meta_blocks
        meta_merkle_blocks = (meta_verity_blocks + 31) // 32
        first_data_blk = meta_merkle_blkaddr + meta_merkle_blocks
    else:
        meta_verity_startblk = 0
        meta_verity_blocks = 0
        meta_merkle_blkaddr = 0
        meta_merkle_blocks = 0
        first_data_blk = 1 + meta_blocks

    # Assign contiguous data block addresses (raw_blkaddr) for directories & regular files
    cur_data_blk = first_data_blk
    data_chunks = []
    node_padded_data = {}
    for node in all_nodes:
        if node.is_dir() or node.is_reg():
            if node.size > 0:
                node.raw_blkaddr = cur_data_blk
                nblks = (node.size + BLOCK_SIZE - 1) // BLOCK_SIZE
                padded = node.data + b"\x00" * (nblks * BLOCK_SIZE - node.size)
                data_chunks.append(padded)
                node_padded_data[node.nid] = (nblks, padded)
                cur_data_blk += nblks
            else:
                node.raw_blkaddr = 0
        elif node.is_chr() or node.is_blk():
            node.raw_blkaddr = node.rdev
        else:
            node.raw_blkaddr = 0

    total_used_blocks = cur_data_blk
    total_blocks = max(total_used_blocks, args.blocks)

    # Assemble the metadata area (meta_blocks * 1024 bytes)
    meta_buf = bytearray(meta_blocks * BLOCK_SIZE)

    build_time = int(os.environ.get("SOURCE_DATE_EPOCH", int(time.time())))
    vol_uuid = uuid_mod.UUID(args.uuid).bytes
    vol_label = args.label.encode("ascii", errors="ignore")[:16].ljust(16, b"\x00")

    # Pack each 32-byte compact inode at offset (node.nid * 32) inside meta_buf
    for node in all_nodes:
        i_format = EROFS_I_VERS_COMPACT | (node.datalayout << 1)
        nlink = (2 + sum(1 for c in node.children.values() if c.is_dir())) if node.is_dir() else 1
        off = node.nid * SLOT_SIZE
        struct.pack_into(
            "<HHHHIIIIHHI",
            meta_buf,
            off,
            i_format,
            0,                 # i_xattr_icount
            node.mode & 0xFFFF,
            nlink,
            node.size,
            0,                 # i_reserved
            node.raw_blkaddr,  # i_u (raw_blkaddr or rdev)
            node.nid,          # i_ino
            node.uid & 0xFFFF,
            node.gid & 0xFFFF,
            0,                 # i_reserved2
        )
        if node.datalayout == EROFS_INODE_FLAT_INLINE and node.inline_data:
            inl_off = off + 32
            meta_buf[inl_off : inl_off + len(node.inline_data)] = node.inline_data
        elif args.verity and node.nid in node_padded_data:
            nblks, padded = node_padded_data[node.nid]
            dig_off = off + 32
            for b_idx in range(nblks):
                blk_bytes = padded[b_idx * BLOCK_SIZE : (b_idx + 1) * BLOCK_SIZE]
                digest = erofs_verity_hash_1k(blk_bytes)
                meta_buf[dig_off + b_idx * 32 : dig_off + (b_idx + 1) * 32] = digest

    # If Native EROFS Verity is enabled, build the metadata Merkle tree over
    # Blocks 2 .. meta_blocks (meta_buf[1024 : meta_blocks * 1024]).
    meta_merkle_buf = bytearray(meta_merkle_blocks * BLOCK_SIZE)
    meta_merkle_root = b"\x00" * 32
    if args.verity and meta_verity_blocks > 0:
        for m_idx in range(meta_verity_blocks):
            m_blk = bytes(meta_buf[(1 + m_idx) * BLOCK_SIZE : (2 + m_idx) * BLOCK_SIZE])
            leaf_digest = erofs_verity_hash_1k(m_blk)
            meta_merkle_buf[m_idx * 32 : (m_idx + 1) * 32] = leaf_digest

        root_blk = bytearray(BLOCK_SIZE)
        for l_idx in range(meta_merkle_blocks):
            l_blk = bytes(meta_merkle_buf[l_idx * BLOCK_SIZE : (l_idx + 1) * BLOCK_SIZE])
            l_digest = erofs_verity_hash_1k(l_blk)
            root_blk[l_idx * 32 : (l_idx + 1) * 32] = l_digest
        meta_merkle_root = erofs_verity_hash_1k(bytes(root_blk))

    # Pack 128-byte struct erofs_super_block at offset 0 of meta_buf (disk offset 1024)
    struct.pack_into(
        "<IIIBBHQQIIII16s16sIHHHBBIQB23s",
        meta_buf,
        0,
        EROFS_SUPER_MAGIC_V1,  # magic
        0,                     # checksum
        0,                     # feature_compat
        BLKSZBITS,             # blkszbits (10 -> 1024B)
        EROFS_VERITY_EXTSLOTS if args.verity else 0,  # extslots (5 -> 80B)
        root.nid,              # root_nid (4 or 32)
        len(all_nodes),        # inos
        build_time,            # build_time
        0,                     # build_time_nsec
        total_used_blocks,     # blocks
        1,                     # meta_blkaddr (block 1 = offset 1024)
        0,                     # xattr_blkaddr
        vol_uuid,              # uuid[16]
        vol_label,             # volume_name[16]
        EROFS_FEATURE_INCOMPAT_VERITY if args.verity else 0,  # feature_incompat
        0,                     # compr_algs
        0,                     # extra_devices
        0,                     # devt_slotoff
        0,                     # dirblkbits
        0,                     # xattr_prefix_count
        0,                     # xattr_prefix_start
        0,                     # packed_nid
        0,                     # xattr_filter_reserved
        b"\x00" * 23,          # reserved[23]
    )

    if args.verity:
        # Pack 80-byte struct erofs_verity_sb_ext immediately after superblock (offset 128)
        struct.pack_into(
            "<IIII32s32s",
            meta_buf,
            128,
            meta_verity_startblk,
            meta_verity_blocks,
            meta_merkle_blkaddr,
            meta_merkle_blocks,
            meta_merkle_root,
            VERITY_SALT,
        )

    # Compute SHA-256 root_digest over Block 1 (the 1024-byte superblock block)
    sb_root_digest_hex = hashlib.sha256(bytes(meta_buf[:BLOCK_SIZE])).digest().hex()
    if args.digest_out:
        os.makedirs(os.path.dirname(os.path.abspath(args.digest_out)), exist_ok=True)
        with open(args.digest_out, "w", encoding="utf-8") as df:
            df.write(sb_root_digest_hex + "\n")

    # Write final image: Block 0 (zeros) + meta_buf + [meta_merkle_buf] + data_chunks + padding
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(b"\x00" * BLOCK_SIZE)  # Block 0
        f.write(meta_buf)
        if args.verity and len(meta_merkle_buf) > 0:
            f.write(meta_merkle_buf)
        for chunk in data_chunks:
            f.write(chunk)
        if total_blocks > total_used_blocks:
            f.write(b"\x00" * ((total_blocks - total_used_blocks) * BLOCK_SIZE))

    return 0


if __name__ == "__main__":
    sys.exit(main())

