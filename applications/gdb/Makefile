include ../../Rules.guest

OBJS	=	gdb.o

gdb: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o .*.o.d gdb
