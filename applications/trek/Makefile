include ../../Rules.guest

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

trek: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o trek
