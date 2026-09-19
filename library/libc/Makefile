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

#
# The objects absorbed from ../sys.
#
# Derived from the *sources* rather than from $(wildcard ../sys/*.o), which
# is what this used to say.  A wildcard over object files is empty on a
# clean tree, and -- much worse -- it made $(TARGET) depend on nothing in
# ../sys at all.  Editing a system call stub rebuilt ../sys/foo.o and
# ../sys/lib/libsys.a, but libc.a, the archive that guest programs are
# actually linked against, was left alone: the fix silently did not reach
# the binaries.  Naming the objects as real prerequisites means make will
# both notice they are out of date and rebuild them, via the %.o: %.c rule
# in Rules.guest -- the same rule and the same flags ../sys/Makefile uses.
#
SYSSRC	=	$(wildcard ../sys/*.c)
SYSOBJS	=	$(SYSSRC:.c=.o)

ARCH	=	$(shell uname -m)
ifeq ($(ARCH),sparc)
OBJS	+=	rem.o urem.o div.o udiv.o mul.o umul.o setjmp.o
SYSOBJS	+=	../sys/sparc.o
else
#
# setjmp/longjmp.  sh(1) cannot be built without them -- every error unwind,
# every break/continue, and the parser's failure path goes through one.
#
# The x86 version is deliberately *not* called setjmp.S: library/libc still
# contains the SPARC setjmp.s, and with both a "%.o: %.S" and a "%.o: %.s"
# rule in scope make would have two ways to produce setjmp.o and would pick
# by rule order rather than by architecture.  Distinct names, no ambiguity.
#
OBJS	+=	setjmp_i386.o
endif

TARGET	=	./lib/libc.a

$(TARGET): $(OBJS) $(SYSOBJS)
	@mkdir -p ./lib
	$(GUEST_AR) -r $(TARGET) $(OBJS) $(SYSOBJS)

clean:
	rm -f *.o $(TARGET)
