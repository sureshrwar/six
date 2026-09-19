#
# The guest C library.
#
# libc.a deliberately contains the system call stubs from ../sys as well,
# so that a guest program can be linked against this one archive and
# nothing else.  That is what every applications/*/Makefile expects.
#
# All the flags live in ../../Rules.guest, including the %.o: %.c rule.
#

include ../../Rules.guest

SRC	=	$(wildcard *.c)
OBJS	=	$(SRC:.c=.o)

SYSOBJS	=	$(wildcard ../sys/*.o)

ARCH	=	$(shell uname -m)
ifeq ($(ARCH),sparc)
OBJS	+=	rem.o urem.o div.o udiv.o mul.o umul.o setjmp.o
endif

TARGET	=	./lib/libc.a

$(TARGET): $(OBJS)
	@mkdir -p ./lib
	$(GUEST_AR) -r $(TARGET) $(OBJS) $(SYSOBJS)

clean:
	rm -f *.o $(TARGET)
