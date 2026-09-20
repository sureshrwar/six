include ../../Rules.guest

OBJS = rmdir.o

rmdir: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) rmdir
