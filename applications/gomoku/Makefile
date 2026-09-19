include ../../Rules.guest

GUEST_CFLAGS += -I.

OBJS = gomoku.o

gomoku: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) gomoku
