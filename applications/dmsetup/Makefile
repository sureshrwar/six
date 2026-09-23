include ../../Rules.guest

OBJS = dmsetup.o

dmsetup: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) dmsetup
