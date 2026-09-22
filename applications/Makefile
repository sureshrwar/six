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
		matrix \
		rogue \
		basic \
		eliza \
		cowsay \
		sl \
		cp \
		mv \
		mkdir \
		rmdir \
		touch \
		df \
		wc \
		head \
		nettest \
		httpd \
		lynx \
		ifconfig \
		sleep \
		telnet \
		telehack \
		testdir \
		testcurses \
		lynx23 \
		starwars \
		irc \
		tcc \
		trek \
		gopher \
		mail \
		more \
		sort \
		uniq \
		tr \
		tail \
		find \
		sed \
		tar \
		sixanim \
		telnetd \
		awk \
		robots \
		zork \
		panic \
		sysctl \
		weather \
		ext4info

all:
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i; done

clean:
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i clean; done
