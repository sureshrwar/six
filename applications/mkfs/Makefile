include ../../Rules.guest

OBJS = mkfs.o

mkfs.ext2: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) mkfs.ext2
