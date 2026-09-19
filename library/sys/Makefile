#
# The guest system call stubs: one file per system call, each of which
# marshals its arguments and calls syscall() in syscall.c.
#
# All the flags live in ../../Rules.guest, including the %.o: %.c rule.
# This Makefile used to carry its own copy, which among other things passed
# -D__KERNEL__ -- wrong for a user program, and the reason several files
# here could get away with including kernel-internal headers.
#

include ../../Rules.guest

SRC	=	$(wildcard *.c)
OBJS	=	$(SRC:.c=.o)

ARCH	=	$(shell uname -m)
ifeq ($(ARCH),sparc)
OBJS	+=	sparc.o
endif

TARGET	=	./lib/libsys.a

$(TARGET): $(OBJS)
	@mkdir -p ./lib
	$(GUEST_AR) -r $(TARGET) $(OBJS)

clean:
	rm -f *.o $(TARGET)
