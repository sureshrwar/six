#
# The shell.
#
# Both architectures now build the real Bourne-shell descendant (sh.c and
# its twelve companions, about 4,500 lines).  Until recently x86 built
# interp.c instead -- a while loop that reads a line and execve()s it --
# because that was where the 2005 port stopped.  /etc/x86.txt on the old
# disk image, now reinstated at port/image/x86.txt, explains it in the
# author's own words:
#
#     "in short - no init, no getty, no login, no nothing - all you have is
#      an ultra-simple shell which sits in a while loop and attempts to
#      execve() whatever you type in."
#
# interp.c was the right thing to bring up first: it exercises the ELF
# loader, the system call path and the console, and nothing else.  But it
# cannot run a script, and /etc/rc is a script, so init(8) needs the real
# shell before the SPARC boot sequence can work on x86 at all.
#
# Two things had to exist first:
#
#   * forward declarations for nine functions that are used before their
#     K&R definitions (var.c, gmatch.c, do.c, glob.c, word.c, here.c).
#     GCC 2.x invented an implicit "int f()" and carried on; a modern
#     compiler rejects the real definition as a conflicting type.
#
#   * setjmp/longjmp.  There was only ever a SPARC implementation
#     (library/libc/setjmp.s); library/libc/setjmp_i386.S is the x86 one.
#     The shell longjmps out of every error, every break and continue, and
#     the parser's failure path.
#
# interp.c is kept.  It is still the fastest way to tell whether a broken
# kernel can load and run *anything*, which is exactly what it was written
# for; build it with "make interp".
#
# Flags come from ../../Rules.guest.
#

include ../../Rules.guest

OBJS	= sh.o var.o gmatch.o area.o csync.o exec.o do.o eval.o glob.o \
	  word.o sh-data.o io.o here.o

sh: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

# The 2005 bring-up shell, kept deliberately.  Not built by default.
interp: interp.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,interp.o)

clean:
	rm -f *.o sh interp
