include ../../Rules.guest

OBJS = storaged.o

storaged: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) storaged
