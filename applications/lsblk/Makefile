include ../../Rules.guest

OBJS = lsblk.o

lsblk: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) lsblk
