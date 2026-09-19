#
# All the compiler and linker flags live in ../../Rules.guest, which also
# supplies the %.o: %.c rule and the guest_link recipe.  This Makefile used
# to carry its own copy of the flags, and no two of them in this tree
# agreed with each other.
#

include ../../Rules.guest

OBJS	=	echo.o

echo: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o echo
