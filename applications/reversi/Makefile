include ../../Rules.guest

GUEST_CFLAGS += -I.

OBJS = reversi.o

reversi: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) reversi
