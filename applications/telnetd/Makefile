include ../../Rules.guest

OBJS = telnetd.o

telnetd: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) telnetd
