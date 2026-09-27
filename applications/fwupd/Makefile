include ../../Rules.guest

OBJS = fwupdmgr.o

fwupdmgr: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) fwupdmgr
