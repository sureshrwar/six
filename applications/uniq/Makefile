include ../../Rules.guest

OBJS = uniq.o

uniq: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) uniq
