include ../../Rules.guest

OBJS = sadbd.o

sadbd: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) sadbd
