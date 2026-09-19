#
# The shell.
#
# Note the asymmetry: SPARC gets the real Bourne-shell descendant (sh.c and
# its thirteen companions), x86 gets interp.c, which is a while loop that
# reads a line and execve()s it.  That was deliberate.  /etc/x86.txt on the
# 2005 disk image -- now reinstated at port/image/x86.txt -- explains it in
# the author's own words:
#
#     "in short - no init, no getty, no login, no nothing - all you have is
#      an ultra-simple shell which sits in a while loop and attempts to
#      execve() whatever you type in."
#
# It is the right thing to bring up first: it exercises the ELF loader, the
# system call path and the console, and nothing else.  Building the full
# shell on x86 is a separate job.
#
# Flags come from ../../Rules.guest.
#

include ../../Rules.guest

ARCH = $(shell uname -m)

ifeq ($(ARCH),sparc)
OBJS	= sh.o var.o gmatch.o area.o csync.o exec.o do.o eval.o glob.o \
	  word.o sh-data.o io.o here.o
else
OBJS	= interp.o
endif

sh: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o sh
