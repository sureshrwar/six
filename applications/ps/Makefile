
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


ps:   ps.o 
	$(LD) $(LDFLAGS) -e cstart ps.o ../start/cstart.o  $(OBJS) -o ps ../../library/libc/lib/libc.a 

clean:
	rm -rf *.o
	rm ps
