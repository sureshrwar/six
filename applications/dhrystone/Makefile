include ../../Rules.guest

OBJS = dhrystone.o

dhrystone: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) dhrystone
