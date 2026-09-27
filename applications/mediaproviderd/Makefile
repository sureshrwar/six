include ../../Rules.guest

OBJS = mediaproviderd.o

mediaproviderd: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) mediaproviderd
