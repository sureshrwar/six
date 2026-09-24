include ../../Rules.guest

OBJS = usbctl.o

usbctl: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) usbctl
