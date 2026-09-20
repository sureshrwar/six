#
# Makefile for standalone Star Wars ASCII player
#

include ../../Rules.guest

OBJS	=	starwars.o

starwars: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o starwars
