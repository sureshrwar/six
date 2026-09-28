include ../../Rules.guest

PLUGIN_OBJS = plugins/nvme/fu-nvme-plugin.o \
	      plugins/ufs/fu-ufs-plugin.o \
	      plugins/scsi/fu-scsi-plugin.o

OBJS = fwupdmgr.o $(PLUGIN_OBJS)

fwupdmgr: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

-include $(wildcard plugins/*/.fu-*.o.d)

clean:
	rm -f $(OBJS) .*.o.d plugins/*/.*.o.d fwupdmgr
