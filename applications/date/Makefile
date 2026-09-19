
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
	$(CC) -DSIX=1 -c -o $@ $< -I../../library/include -I../../include


date:   date.o 
	$(LD) $(LDFLAGS) -e cstart date.o ../start/cstart.o  $(OBJS) -o date ../../library/libc/lib/libc.a 

clean:
	rm -rf *.o
	rm date
