include ../../Rules.guest

EXTRA_CXXFLAGS = -I$(SIX_GUEST_TOP)/applications/vold/compat

OBJS = main.o \
       VolumeManager.o \
       NetlinkManager.o \
       NetlinkHandler.o \
       VoldNativeService.o \
       Utils.o \
       model/Disk.o \
       model/VolumeBase.o \
       model/PublicVolume.o \
       model/PrivateVolume.o \
       model/EmulatedVolume.o \
       fs/Ext4.o \
       fs/Ext2.o \
       fs/Ntfs.o \
       compat/six_cxx_runtime.o

vold: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) vold .*.o.d model/.*.o.d fs/.*.o.d compat/.*.o.d
