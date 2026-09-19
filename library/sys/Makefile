
CC	=	gcc 
LD	= 	ld
AR	=	ar

LDFLAGS = 	-M map/user.map

SRC     =       $(shell find . -name "*.c")

TARGET	=	./lib/libsys.a

%.o: %.c
	$(CC)  -D__KERNEL__ -DSIX=1 -c -o $@ $< -I../include -I../../include
%.o: %.s
	$(CC) -c -o $@ $<

OBJS    =       $(subst .c,.o,$(SRC))

ARCH = $(shell uname -p)

ifeq ($(ARCH),sparc)
	OBJS += sparc.o
endif

$(TARGET): $(OBJS) 
	$(AR) -r ./lib/libsys.a $(OBJS) 
	

clean:
	rm -rf *.o
