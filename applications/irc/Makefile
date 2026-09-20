#
# Makefile for SIX IRC client (TinyIRC)
#

include ../../Rules.guest

OBJS	= irc.o

GUEST_CFLAGS += -DPOSIX -DDEFAULTSERVER=\"irc.dal.net\" -DDEFAULTPORT=6667

irc: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o irc
