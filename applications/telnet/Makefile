include ../../Rules.guest

OBJS	=	telnet.o

telnet: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o telnet
