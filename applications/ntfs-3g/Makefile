include ../../Rules.guest

GUEST_CFLAGS += -I. \
		-Iinclude/compat \
		-Iinclude/fuse-lite \
		-Iinclude/ntfs-3g \
		-Intfsprogs \
		-Isrc \
		-DHAVE_CONFIG_H \
		-DFUSE_USE_VERSION=26 \
		-Dlinux=1 \
		-D__linux__=1

FUSE_OBJS = \
	libfuse-lite/fuse.o \
	libfuse-lite/fuse_kern_chan.o \
	libfuse-lite/fuse_loop.o \
	libfuse-lite/fuse_lowlevel.o \
	libfuse-lite/fuse_opt.o \
	libfuse-lite/fuse_session.o \
	libfuse-lite/fuse_signals.o \
	libfuse-lite/fusermount.o \
	libfuse-lite/helper.o \
	libfuse-lite/mount.o \
	libfuse-lite/mount_util.o

NTFS_OBJS = \
	libntfs-3g/acls.o \
	libntfs-3g/attrib.o \
	libntfs-3g/attrlist.o \
	libntfs-3g/bitmap.o \
	libntfs-3g/bootsect.o \
	libntfs-3g/cache.o \
	libntfs-3g/collate.o \
	libntfs-3g/compat.o \
	libntfs-3g/compress.o \
	libntfs-3g/debug.o \
	libntfs-3g/device.o \
	libntfs-3g/dir.o \
	libntfs-3g/ea.o \
	libntfs-3g/efs.o \
	libntfs-3g/index.o \
	libntfs-3g/inode.o \
	libntfs-3g/ioctl.o \
	libntfs-3g/lcnalloc.o \
	libntfs-3g/logfile.o \
	libntfs-3g/logging.o \
	libntfs-3g/mft.o \
	libntfs-3g/misc.o \
	libntfs-3g/mst.o \
	libntfs-3g/object_id.o \
	libntfs-3g/realpath.o \
	libntfs-3g/reparse.o \
	libntfs-3g/runlist.o \
	libntfs-3g/security.o \
	libntfs-3g/unistr.o \
	libntfs-3g/unix_io.o \
	libntfs-3g/volume.o \
	libntfs-3g/xattrs.o

MAIN_OBJS = \
	src/ntfs-3g.o \
	src/ntfs-3g_common.o \
	six_compat.o

NTFSPROGS_BINS = \
	mkntfs \
	ntfsfix \
	ntfsinfo \
	ntfslabel \
	ntfsls \
	ntfscat \
	ntfscluster \
	ntfscmp \
	ntfscp \
	ntfsresize \
	ntfsclone \
	ntfsundelete

NTFSPROGS_OBJS = \
	ntfsprogs/attrdef.o \
	ntfsprogs/boot.o \
	ntfsprogs/cluster.o \
	ntfsprogs/sd.o \
	ntfsprogs/utils.o \
	ntfsprogs/mkntfs.o \
	ntfsprogs/ntfsfix.o \
	ntfsprogs/ntfsinfo.o \
	ntfsprogs/ntfslabel.o \
	ntfsprogs/ntfsls.o \
	ntfsprogs/ntfscat.o \
	ntfsprogs/ntfscluster.o \
	ntfsprogs/ntfscmp.o \
	ntfsprogs/ntfscp.o \
	ntfsprogs/ntfsresize.o \
	ntfsprogs/ntfsclone.o \
	ntfsprogs/ntfsundelete.o

OBJS = $(MAIN_OBJS) $(FUSE_OBJS) $(NTFS_OBJS)

all: ntfs-3g ntfs-3g.probe $(NTFSPROGS_BINS)

ntfs-3g: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))
	strip $@

ntfs-3g.probe: src/ntfs-3g.probe.o $(NTFS_OBJS) six_compat.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,src/ntfs-3g.probe.o $(NTFS_OBJS) six_compat.o)
	strip $@

mkntfs: ntfsprogs/attrdef.o ntfsprogs/boot.o ntfsprogs/sd.o ntfsprogs/mkntfs.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,ntfsprogs/attrdef.o ntfsprogs/boot.o ntfsprogs/sd.o ntfsprogs/mkntfs.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o)
	strip $@

ntfsfix: ntfsprogs/ntfsfix.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,ntfsprogs/ntfsfix.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o)
	strip $@

ntfsinfo: ntfsprogs/ntfsinfo.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,ntfsprogs/ntfsinfo.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o)
	strip $@

ntfslabel: ntfsprogs/ntfslabel.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,ntfsprogs/ntfslabel.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o)
	strip $@

ntfsls: ntfsprogs/ntfsls.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,ntfsprogs/ntfsls.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o)
	strip $@

ntfscat: ntfsprogs/ntfscat.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,ntfsprogs/ntfscat.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o)
	strip $@

ntfscluster: ntfsprogs/ntfscluster.o ntfsprogs/cluster.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,ntfsprogs/ntfscluster.o ntfsprogs/cluster.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o)
	strip $@

ntfscmp: ntfsprogs/ntfscmp.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,ntfsprogs/ntfscmp.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o)
	strip $@

ntfscp: ntfsprogs/ntfscp.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,ntfsprogs/ntfscp.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o)
	strip $@

ntfsresize: ntfsprogs/ntfsresize.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,ntfsprogs/ntfsresize.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o)
	strip $@

ntfsclone: ntfsprogs/ntfsclone.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,ntfsprogs/ntfsclone.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o)
	strip $@

ntfsundelete: ntfsprogs/ntfsundelete.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,ntfsprogs/ntfsundelete.o ntfsprogs/utils.o $(NTFS_OBJS) six_compat.o)
	strip $@

clean:
	rm -f $(OBJS) $(NTFSPROGS_OBJS) src/ntfs-3g.probe.o ntfs-3g ntfs-3g.probe $(NTFSPROGS_BINS)
