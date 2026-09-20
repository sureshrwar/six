include ../../Rules.guest

OBJS = eliza.o

eliza: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) eliza
