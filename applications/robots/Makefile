#
# BSD Robots for SIX
#

include ../../Rules.guest

GUEST_CFLAGS += -DMAX_PER_UID=5 -DFANCY

OBJS = auto.o extern.o flush_in.o init_field.o main.o make_level.o move.o move_robs.o play_level.o query.o rnd_pos.o score.o

robots: $(OBJS) $(GUEST_CRT) $(GUEST_LIBCURSES) $(GUEST_LIBC)
	$(call guest_link_curses,$@,$(OBJS))

clean:
	rm -f *.o robots
