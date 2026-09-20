include ../../Rules.guest

OBJS	=	testcurses.o

testcurses: $(OBJS) $(GUEST_CRT) $(GUEST_LIBCURSES) $(GUEST_LIBC)
	$(call guest_link_curses,$@,$(OBJS))

clean:
	rm -f *.o testcurses
