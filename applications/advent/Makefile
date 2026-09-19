include ../../Rules.guest

GUEST_CFLAGS += -I. -D_POSIX_SOURCE -DTEXTDIR='"/usr/lib/advent/"'

OBJS = advent.o database.o english.o initial.o itverb.o score.o \
	travel.o turn.o utility.o verb.o vocab.o six_compat.o

DAT = advent1.dat advent2.dat advent3.dat advent4.dat

TARGET = advent

all: $(DAT) $(TARGET)

setup: setup.c advent.h
	gcc -O2 -w -o setup setup.c

advtext.h $(DAT): setup advent1.txt advent2.txt advent3.txt advent4.txt
	./setup

database.o: advtext.h

$(TARGET): $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) $(TARGET) setup advtext.h $(DAT)
