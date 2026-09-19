#
# Makefile for some libs needed in the kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

ifdef SOLARIS_USER_MODE
O_TARGET := lib.o
O_OBJS   := errno.o ctype.o string.o vsprintf.o
else
L_TARGET := lib.a
L_OBJS   := errno.o ctype.o string.o vsprintf.o
endif

include $(TOPDIR)/Rules.make
