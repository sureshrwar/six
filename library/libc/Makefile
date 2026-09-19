CC	=	gcc 
LD	= 	/usr/ccs/bin/ld
AR	=	ar

SRC	=	$(shell find . -name "*.c")
TARGET  =	./lib/libc.a

%.o: %.c
	$(CC) -g -DSIX=1 -I../include -I../../include -c -o $@ $<
%.o: %.s
	$(CC) -c -o $@ $<

OBJS	=	$(subst .c,.o,$(SRC))	
SOBJS	=	../sys/*.o
ARCH = $(shell uname -p)

ifeq ($(ARCH),sparc)
        OBJS += rem.o
        OBJS += urem.o
        OBJS += div.o
        OBJS += udiv.o
        OBJS += mul.o
        OBJS += umul.o
        OBJS += setjmp.o
endif

$(TARGET): $(OBJS)
	$(AR) -r  ./lib/libc.a $(OBJS) $(SOBJS)
clean:
	rm -rf *.o
	rm ./lib/libc.a
