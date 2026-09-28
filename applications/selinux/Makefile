include ../../Rules.guest

OBJS = selinux.o

selinux: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) selinux
