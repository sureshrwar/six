include ../../Rules.guest

OBJS	=	sleep.o

sleep: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o sleep
