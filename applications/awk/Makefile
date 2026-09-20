#
# One True Awk (Brian Kernighan) for SIX
#

include ../../Rules.guest

OBJS = ytab.o b.o main.o parse.o proctab.o tran.o lib.o run.o lex.o

awk: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o awk
