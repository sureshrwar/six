include ../../Rules.guest

OBJS	=	sysctl.o

sysctl: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o sysctl
