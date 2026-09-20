include ../../Rules.guest

OBJS	=	testdir.o

testdir: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o testdir
