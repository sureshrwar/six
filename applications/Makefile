#
# The guest userland.
#
# start must be built first: every other program links against
# start/cstart.o.
#
# banner, cat and halt are deliberately not in this list, which is how the
# 2005 tree had it.  They are still buildable by hand.
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
		last

all:
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i; done

clean:
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i clean; done
