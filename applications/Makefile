
CC	=	gcc
LD	= 	ld
AR	=	ar

ARCH = $(shell uname -p)

ifeq ($(ARCH),sparc)
LDFLAGS = 	-M ../map/sparc/user.map
else
LDFLAGS = 	-M ../map/x86/user.map
endif



SUBDIRS	= 	start\
		init\
		getty\
		login\
		sh\
		hello\
		fortune\
		clear \
		ps \
		ls\
		tty\
		rm \
		sh \
		echo \
		pwd \
		sync \
		sethostname \
		kill \
		grep \
		date \
		id \
		last


%.o: %.c
	$(CC)  -c -o $@ $< -I../include


all:
	for i in $(SUBDIRS); do gmake -C $$i; done
clean:
	for i in $(SUBDIRS); do gmake -C $$i clean; done
	
