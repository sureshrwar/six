include ../../Rules.guest

OBJS = tetris.o

tetris: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) tetris
