
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


fortune:   fortune.o 
	$(LD) $(LDFLAGS) -e cstart fortune.o ../start/cstart.o  $(OBJS) -o fortune ../../library/libc/lib/libc.a 

clean:
	rm -rf *.o
	rm fortune
