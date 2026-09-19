include ../../Rules.guest

GUEST_CFLAGS += -I.

OBJS = life.o

life: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) life
