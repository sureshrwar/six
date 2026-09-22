include ../../Rules.guest

OBJS = dd.o

dd: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) dd
