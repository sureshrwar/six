include ../../Rules.guest

OBJS	=	panic.o

panic: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o panic
