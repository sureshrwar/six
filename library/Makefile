#
# The guest C library.
#
# sys must be built before libc: libc.a absorbs ../sys/*.o so that guest
# programs can link against one archive.
#

SUBDIRS	=	sys \
		libc

all:
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i; done

clean:
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i clean; done
