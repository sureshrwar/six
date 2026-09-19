#!/usr/bin/env python3
"""
ext2ls.py -- list the contents of a rev-0 ext2 image of *either* byte order.

Why this exists
---------------
SIX has two root filesystem images in the 2005 tree, disk/sparc/root and
disk/x86/root.  Only one of them can be read by the host's e2fsprogs:

    $ file sparc.img x86.img
    sparc.img: data
    x86.img:   Linux rev 0.0 ext2 filesystem data

The SPARC image is big-endian.  Linux 2.0's ext2 predates the byte-order
normalisation that later kernels do (the le32_to_cpu() sprinkling), so on a
big-endian host it simply wrote native-endian metadata.  The magic number
proves it: 0xef53 appears as "ef 53" in the SPARC image and "53 ef" in the
x86 one.  Modern e2fsprogs dropped the --swapfs support that used to cope
with this, so nothing on a gLinux box will open it.

Parsing it directly is not hard -- rev 0 ext2 with no feature flags is a
small, fixed format -- and it is the only way to see how the SPARC system
was actually laid out, which is the reference for making x86 behave the
same way.

Usage:  ext2ls.py <image> [--be] [--cat GLOB]
"""

import struct
import sys
import stat as statmod
import fnmatch
import time

BLOCK_SIZE = 1024          # rev 0, s_log_block_size == 0
INODE_SIZE = 128           # rev 0 is always 128
ROOT_INO = 2


class Ext2:
    def __init__(self, path, big_endian):
        with open(path, "rb") as f:
            self.data = f.read()
        self.e = ">" if big_endian else "<"
        self._read_superblock()
        self._read_group_descs()

    # --- primitive readers -------------------------------------------------
    def u16(self, off):
        return struct.unpack_from(self.e + "H", self.data, off)[0]

    def u32(self, off):
        return struct.unpack_from(self.e + "I", self.data, off)[0]

    def block(self, n):
        return self.data[n * BLOCK_SIZE:(n + 1) * BLOCK_SIZE]

    # --- metadata ----------------------------------------------------------
    def _read_superblock(self):
        sb = 1024
        self.inodes_count = self.u32(sb + 0x00)
        self.blocks_count = self.u32(sb + 0x04)
        self.r_blocks = self.u32(sb + 0x08)
        self.free_blocks = self.u32(sb + 0x0C)
        self.free_inodes = self.u32(sb + 0x10)
        self.first_data_block = self.u32(sb + 0x14)
        self.log_block_size = self.u32(sb + 0x18)
        self.blocks_per_group = self.u32(sb + 0x20)
        self.inodes_per_group = self.u32(sb + 0x28)
        self.mnt_count = self.u16(sb + 0x34)
        self.max_mnt_count = self.u16(sb + 0x36)
        self.magic = self.u16(sb + 0x38)
        self.state = self.u16(sb + 0x3A)
        self.lastcheck = self.u32(sb + 0x40)
        self.creator_os = self.u32(sb + 0x48)
        self.rev_level = self.u32(sb + 0x4C)
        if self.magic != 0xEF53:
            raise SystemExit("bad magic %04x -- wrong byte order?" % self.magic)

    def _read_group_descs(self):
        # The group descriptor table lives in the block after the superblock.
        gd_block = self.first_data_block + 1
        self.groups = []
        ngroups = (self.blocks_count - self.first_data_block +
                   self.blocks_per_group - 1) // self.blocks_per_group
        for g in range(ngroups):
            off = gd_block * BLOCK_SIZE + g * 32
            self.groups.append({
                "block_bitmap": self.u32(off + 0),
                "inode_bitmap": self.u32(off + 4),
                "inode_table": self.u32(off + 8),
            })

    def inode(self, ino):
        g = (ino - 1) // self.inodes_per_group
        idx = (ino - 1) % self.inodes_per_group
        off = self.groups[g]["inode_table"] * BLOCK_SIZE + idx * INODE_SIZE
        return {
            "mode": self.u16(off + 0),
            "uid": self.u16(off + 2),
            "size": self.u32(off + 4),
            "mtime": self.u32(off + 16),
            "gid": self.u16(off + 24),
            "links": self.u16(off + 26),
            "blocks": self.u32(off + 28),
            "block": [self.u32(off + 40 + 4 * i) for i in range(15)],
            "raw_block_bytes": self.data[off + 40:off + 100],
        }

    # --- data --------------------------------------------------------------
    def read_file(self, ino_data):
        size = ino_data["size"]
        out = b""
        blocks = ino_data["block"]
        # direct
        for b in blocks[:12]:
            if b:
                out += self.block(b)
        # single indirect
        if blocks[12]:
            ib = self.block(blocks[12])
            for i in range(0, BLOCK_SIZE, 4):
                b = struct.unpack_from(self.e + "I", ib, i)[0]
                if b:
                    out += self.block(b)
        # double indirect
        if blocks[13]:
            ib = self.block(blocks[13])
            for i in range(0, BLOCK_SIZE, 4):
                b1 = struct.unpack_from(self.e + "I", ib, i)[0]
                if not b1:
                    continue
                ib2 = self.block(b1)
                for j in range(0, BLOCK_SIZE, 4):
                    b2 = struct.unpack_from(self.e + "I", ib2, j)[0]
                    if b2:
                        out += self.block(b2)
        return out[:size]

    def readdir(self, ino_data):
        """Yield (name, inode).  rev 0 has a 16-bit name_len and no file_type."""
        data = self.read_file(ino_data)
        entries = []
        off = 0
        while off < len(data):
            ino = struct.unpack_from(self.e + "I", data, off)[0]
            rec_len = struct.unpack_from(self.e + "H", data, off + 4)[0]
            name_len = struct.unpack_from(self.e + "H", data, off + 6)[0]
            if rec_len == 0:
                break
            if ino:
                name = data[off + 8:off + 8 + (name_len & 0xFF)]
                entries.append((name.decode("latin-1"), ino))
            off += rec_len
        return entries

    def readlink(self, ino_data):
        if ino_data["blocks"] == 0:
            return ino_data["raw_block_bytes"][:ino_data["size"]].decode("latin-1")
        return self.read_file(ino_data).decode("latin-1")


def typechar(mode):
    if statmod.S_ISDIR(mode):
        return "d"
    if statmod.S_ISLNK(mode):
        return "l"
    if statmod.S_ISCHR(mode):
        return "c"
    if statmod.S_ISBLK(mode):
        return "b"
    if statmod.S_ISFIFO(mode):
        return "p"
    if statmod.S_ISSOCK(mode):
        return "s"
    return "-"


def walk(fs, ino, path, out):
    ind = fs.inode(ino)
    for name, child in sorted(fs.readdir(ind)):
        if name in (".", ".."):
            continue
        cin = fs.inode(child)
        full = (path + "/" + name).replace("//", "/")
        m = cin["mode"]
        desc = ""
        if statmod.S_ISCHR(m) or statmod.S_ISBLK(m):
            dev = cin["block"][0]
            desc = "  dev %d,%d" % ((dev >> 8) & 0xFF, dev & 0xFF)
        elif statmod.S_ISLNK(m):
            desc = "  -> " + fs.readlink(cin)
        out.append((full, typechar(m), statmod.S_IMODE(m), cin["uid"],
                    cin["gid"], cin["size"], cin["links"], child,
                    cin["mtime"], desc))
        if statmod.S_ISDIR(m):
            walk(fs, child, full, out)


def main():
    args = sys.argv[1:]
    if not args:
        raise SystemExit(__doc__)
    path = args[0]
    big = "--be" in args
    catglob = None
    if "--cat" in args:
        catglob = args[args.index("--cat") + 1]

    fs = Ext2(path, big)

    print("image            : %s  (%s-endian)" % (path, "big" if big else "little"))
    print("revision         : %d" % fs.rev_level)
    print("block size       : %d" % (1024 << fs.log_block_size))
    print("blocks           : %d  (%d free)" % (fs.blocks_count, fs.free_blocks))
    print("inodes           : %d  (%d free)" % (fs.inodes_count, fs.free_inodes))
    print("mount count      : %d of %d" % (fs.mnt_count, fs.max_mnt_count))
    print("state            : %d (%s)" % (fs.state,
                                          "clean" if fs.state == 1 else "not clean"))
    print("last checked     : %s" % time.strftime(
        "%Y-%m-%d %H:%M", time.gmtime(fs.lastcheck)))
    print()

    out = []
    walk(fs, ROOT_INO, "", out)

    if catglob:
        for (full, t, mode, uid, gid, size, links, ino, mtime, desc) in out:
            if fnmatch.fnmatch(full, catglob) and t == "-":
                print("========== %s (%d bytes) ==========" % (full, size))
                body = fs.read_file(fs.inode(ino))
                try:
                    print(body.decode("utf-8"))
                except UnicodeDecodeError:
                    print(body.decode("latin-1"))
        return

    print("%-24s %s %-5s %4s %4s %9s %5s %6s  %s" %
          ("path", "t", "mode", "uid", "gid", "size", "links", "inode", "notes"))
    print("-" * 100)
    for (full, t, mode, uid, gid, size, links, ino, mtime, desc) in out:
        print("%-24s %s %05o %4d %4d %9d %5d %6d  %s%s" %
              (full, t, mode, uid, gid, size, links, ino,
               time.strftime("%Y-%m-%d", time.gmtime(mtime)), desc))
    print()
    print("%d entries" % len(out))


if __name__ == "__main__":
    main()
