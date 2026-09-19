
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
	$(CC) -w -DSIX=1  -c -o $@ $< -I../../library/include -I../../include


ls:   ls.o 
	$(LD) $(LDFLAGS) -e cstart ls.o ../start/cstart.o  $(OBJS) -o ls ../../library/libc/lib/libc.a 

clean:
	rm -rf *.o
	rm ls
