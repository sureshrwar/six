include ../../Rules.guest

OBJS = mkdir.o

mkdir: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) mkdir
