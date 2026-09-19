include ../../Rules.guest

GUEST_CFLAGS += -I. -funsigned-char -DTMPDIR='"/tmp"' -D_POSIX_SOURCE -D_MINIX -DCRUNCH \
	-DNO_MKEXRC -DNO_CURSORSHAPE -DNO_CHARATTR -DNO_SHOWMODE \
	-DNO_MODELINE -DNO_OPTCOLS -DNO_DIGRAPH -DNO_EXTENSIONS \
	-DNO_ERRLIST -DNO_FKEY -DNO_VISIBLE -DNO_COLOR -DNO_POPUP

OBJS = blk.o cmd1.o cmd2.o ctype.o curses.o cut.o ex.o input.o \
	main.o misc.o modify.o move1.o move2.o move3.o move4.o move5.o \
	opts.o recycle.o redraw.o regexp.o regsub.o system.o tio.o tmp.o \
	unix.o vars.o vcmd.o vi.o six_compat.o

TARGET = vi

$(TARGET): $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) $(TARGET)
