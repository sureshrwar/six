include ../../Rules.guest

OBJS	=	nettest.o

nettest: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o nettest
