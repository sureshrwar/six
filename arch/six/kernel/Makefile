#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

#.S.s:
#	$(CPP) -D__ASSEMBLY__ -traditional $< -o $*.s

ifdef SMP

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c $< -o $*.o
else
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c $< -o $*.o
endif

ifdef SOLARIS_USER_MODE
all: kernel.o 
else
all: kernel.o head.o
endif

O_TARGET := kernel.o

ifdef SOLARIS_USER_MODE
O_OBJS   := process.o host.o signal.o traps.o irq.o vm86.o bios32.o \
            ptrace.o ioport.o ldt.o setup.o time.o sys_six.o ksyms.o 
else
O_OBJS   := process.o host.o signal.o entry.o traps.o irq.o vm86.o bios32.o \
            ptrace.o ioport.o ldt.o setup.o time.o sys_i386.o ksyms.o 
endif

ifdef SMP

O_OBJS += smp.o

head.o: head.S $(TOPDIR)/include/linux/tasks.h
	$(CC) -D__ASSEMBLY__ -D__SMP__ -traditional -c $*.S -o $*.o

else

head.o: head.S $(TOPDIR)/include/linux/tasks.h
	$(CC) -D__ASSEMBLY__ -traditional -c $*.S -o $*.o

endif

hexify:
	$(HOSTCC) hexify.c -o hexify

smp.c: trampoline.hex

trampoline.hex: trampoline hexify
	(dd if=trampoline bs=1 skip=32 | ./hexify >trampoline.hex )

trampoline: trampoline.o trampoline32.o
	$(LD86) -s -o $@ trampoline.o trampoline32.o

trampoline.o: trampoline.s
	$(AS86) -o $@ $<

trampoline32.o: trampoline32.s
	$(AS386) -o $@ $<

trampoline.s: trampoline.S $(CONFIGURE) $(TOPDIR)/include/linux/config.h Makefile
	$(CPP) -D__SMP__ -traditional $< -o $@

trampoline32.s: trampoline32.S $(CONFIGURE) $(TOPDIR)/include/linux/config.h Makefile
	$(CPP) -D__SMP__ -traditional $< -o $@

clean:
	rm -f trampoline hexify .trace_flag

#
# irq.c is the only file that reads SIX_TRACE_GUEST_SYSCALLS (see the top
# level Makefile, which turns it into a -D in CFLAGS).  Make has no way of
# noticing that a command line -D changed -- irq.o is still newer than
# irq.c -- so "make SIX_TRACE_GUEST_SYSCALLS=0" after a traced build would
# quietly do nothing and leave a binary that still traces.  That is a
# genuinely confusing thing to debug.
#
# Record the value in a stamp file, rewritten only when it actually
# changes, and hang irq.o off it.  FORCE makes the stamp rule run every
# time; cmp keeps the timestamp still unless the contents differ.
#
.PHONY: FORCE
FORCE:

.trace_flag: FORCE
	@echo '$(SIX_TRACE_GUEST_SYSCALLS)' | cmp -s - $@ 2>/dev/null || \
		echo '$(SIX_TRACE_GUEST_SYSCALLS)' > $@

irq.o: .trace_flag

include $(TOPDIR)/Rules.make
