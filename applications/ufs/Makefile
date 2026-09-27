include ../../Rules.guest

OBJS = ufs.o

ufs: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) ufs
