include ../../Rules.guest

OBJS = rogue.o

rogue: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) rogue
