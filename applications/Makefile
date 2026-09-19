#
# The guest userland.
#
# start must be built first: every other program links against
# start/cstart.o.
#
# The list is the 23 programs found in /bin on the 2005 SPARC root disk,
# plus init.  banner, cat and halt used to be missing from here -- they are
# on that disk, and their Makefiles work, they had simply fallen out of the
# build at some point and nobody noticed because x86 shipped eight programs
# in total.
#

SUBDIRS	=	start \
		init \
		getty \
		login \
		sh \
		hello \
		fortune \
		clear \
		ps \
		ls \
		tty \
		rm \
		echo \
		pwd \
		sync \
		sethostname \
		kill \
		grep \
		date \
		id \
		last \
		banner \
		cat \
		halt \
		vi \
		advent \
		life \
		ttt \
		gomoku \
		cal \
		dhrystone \
		tetris \
		matrix

all:
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i; done

clean:
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i clean; done
