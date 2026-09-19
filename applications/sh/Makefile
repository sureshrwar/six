
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
	$(CC) -w -c -o $@ $< -I../../library/include -I../../include

ARCH = $(shell uname -p)
ifeq ($(ARCH),sparc)
OBJS     = sh.o var.o gmatch.o area.o csync.o exec.o do.o eval.o glob.o \
	   word.o sh-data.o io.o here.o
else
OBJS	 = interp.o  
endif



sh:   $(OBJS)
	$(LD) $(LDFLAGS) -e cstart ../start/cstart.o  $(OBJS) -o sh ../../library/libc/lib/libc.a 

clean:
	rm -rf *.o
	rm sh
