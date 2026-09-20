include ../../Rules.guest

OBJS	=	ifconfig.o

ifconfig: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o ifconfig
