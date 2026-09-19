
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


sethostname:   sethostname.o 
	$(LD) $(LDFLAGS) -e cstart sethostname.o ../start/cstart.o  $(OBJS) -o sethostname ../../library/libc/lib/libc.a 

clean:
	rm -rf *.o
	rm sethostname
