
CC	=	gcc
LD	= 	ld
AR	=	ar

ARCH = $(shell uname -p)

ifeq ($(ARCH),sparc)
LDFLAGS =       -M ../map/sparc/user.map
else
LDFLAGS =       -M ../map/x86/user.map
endif

%.o: %.c
	$(CC)  -c -o $@ $< -I../../library/include -I../../include


grep:   grep.o 
	$(LD) $(LDFLAGS) -e cstart grep.o ../start/cstart.o  $(OBJS) -o grep ../../library/libc/lib/libc.a 

clean:
	rm -rf *.o
	rm grep
