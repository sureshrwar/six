include ../../Rules.guest

OBJS = umount.o

umount: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) umount
