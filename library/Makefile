
CC	=	gcc
LD	= 	ld
AR	=	ar

LDFLAGS = 	-M ../map/user.map

SUBDIRS	=	sys \
		libc


all:
	for i in $(SUBDIRS); do make -C $$i; done
clean:
	for i in $(SUBDIRS); do make -C $$i clean; done
	
