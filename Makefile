# Yes this is SIX
SOLARIS_USER_MODE = yes

VERSION = 2
PATCHLEVEL = 0
SUBLEVEL = 11

ifndef SOLARIS_USER_MODE
ARCH = i386
else
# solaris stuff..
ARCH = six

CONFIG_INET = y
CONFIG_BINFMT_ELF = y
CONFIG_EXT2_FS = y

endif

TOPDIR	:= $(shell if [ "$$PWD" != "" ]; then echo $$PWD; else pwd; fi)

#
# For SMP kernels, set this. We don't want to have this in the config file
# because it makes re-config very ugly and too many fundamental files depend
# on "CONFIG_SMP"
#
# NOTE! SMP is experimental. See the file Documentation/SMP.txt
#
# SMP = 1
#
# SMP profiling options
# SMP_PROF = 1

.EXPORT_ALL_VARIABLES:

ifndef SOLARIS_USER_MODE
CONFIG_SHELL := $(shell if [ -x "$$BASH" ]; then echo $$BASH; \
	  else if [ -x /bin/bash ]; then echo /bin/bash; \
	  else echo sh; fi ; fi)
TOPDIR	:= $(shell if [ "$$PWD" != "" ]; then echo $$PWD; else pwd; fi)

else
CONFIG_SHELL := /bin/bash
TOPDIR	:= $(shell if [ "$$PWD" != "" ]; then echo $$PWD; else pwd; fi)
ROOT = $(TOPDIR)
endif

HPATH   	= $(TOPDIR)/include

HOSTCC  	=gcc -I$(HPATH)
HOSTCFLAGS	=

CROSS_COMPILE 	=

AS	=$(CROSS_COMPILE)as
ifndef SOLARIS_USER_MODE
AR	=$(CROSS_COMPILE)ar
LD	=$(CROSS_COMPILE)ld
CC	=$(CROSS_COMPILE)gcc -D__KERNEL__  -DSIX=0 -I$(HPATH)
else
AR	=$(CROSS_COMPILE)ar
LD	=$(CROSS_COMPILE)ld -m elf_i386
CC	=$(CROSS_COMPILE)gcc -m32 -D__KERNEL__ -DSIX=1 -I$(HPATH)
endif
CPP	=$(CC) -E
NM	=$(CROSS_COMPILE)nm
STRIP	=$(CROSS_COMPILE)strip
ifndef SOLARIS_USER_MODE
MAKE	=make
else
MAKE	=make
endif
AWK	=awk

all:	do-it-all

#
# Make "config" the default target if there is no configuration file or
# "depend" the target if there is no top-level dependency information.
#

ifndef SOLARIS_USER_MODE
ifeq (.config,$(wildcard .config))
include .config
ifeq (.depend,$(wildcard .depend))
include .depend
do-it-all:	Version vmlinux
else
CONFIGURATION = depend
do-it-all:	depend
endif
else
CONFIGURATION = config
do-it-all:	config
endif
else
# The disk image is a build product now, exactly like the kernel binary.  It
# used to be a 5 MB blob checked into CVS in 2005; see the "guest root
# filesystem image" section further down for the rules and the reasoning.
#
# This has to be defined here rather than next to those rules: make expands
# a rule's prerequisites at the moment it reads the rule, so a variable used
# on the right-hand side of "do-it-all:" must already have a value.
SIX_IMAGE	= disk/x86/root
do-it-all:	include/asm Version six $(SIX_IMAGE)
	@echo ""
	@echo "======================================================================"
	@echo "  Build successful! Your early-2000s time machine is ready."
	@echo ""
	@echo "  To boot into SIX, run:"
	@echo "      ./six"
	@echo ""
	@echo "  Login as 'root' (no password)."
	@echo "  When you're done, run 'halt' or press Ctrl+] to return to the 2020s."
	@echo "======================================================================"
	@echo ""

include/asm:
	ln -sfn asm-$(ARCH) include/asm
endif

#
# ROOT_DEV specifies the default root-device when making the image.
# This can be either FLOPPY, CURRENT, /dev/xxxx or empty, in which case
# the default of FLOPPY is used by 'build'.
#

ROOT_DEV = CURRENT

#
# INSTALL_PATH specifies where to place the updated kernel and system map
# images.  Uncomment if you want to place them anywhere other than root.

ifndef SOLARIS_USER_MODE
#INSTALL_PATH=/
else
INSTALL_PATH=$(ROOT)
endif

#
# If you want to preset the SVGA mode, uncomment the next line and
# set SVGA_MODE to whatever number you want.
# Set it to -DSVGA_MODE=NORMAL_VGA if you just want the EGA/VGA mode.
# The number is the same as you would ordinarily press at bootup.
#

SVGA_MODE=	-DSVGA_MODE=NORMAL_VGA

#
# standard CFLAGS
#

#CFLAGS = -g  -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -fno-strength-reduce

#
# GCC-15 compatibility set for this 1996-era source base.
#
#   -std=gnu89            K&R-style definitions, implicit int, old for-scoping
#   -fcommon              tentative definitions merge (pre-GCC-10 default); the
#                         kernel declares the same global in several .c files
#   -fno-strict-aliasing  pervasive type punning through pointer casts
#   -fno-builtin          don't let gcc substitute host libc semantics for the
#                         kernel's own memcpy/strlen/printf
#   -no-pie / -fno-pic    the kernel mmaps "physical RAM" MAP_FIXED at an
#                         address derived from &_end, so it must be non-PIE
#   -Wno-error=...        GCC 14+ promoted these to hard errors; this code
#                         predates the relevant standards, so demote them again
#
#
# Set to 1 to have system_call() print every system call a guest makes.
# Off by default; it is very noisy and the console is slow (one write(2)
# per character, and a full re-blit of the screen on every scroll).
#
# Only arch/six/kernel/irq.c reads it.  Changing a -D on the command line
# is invisible to make -- the .o is newer than the .c either way -- so the
# value is recorded in a stamp file that irq.o depends on; see
# arch/six/kernel/Makefile.  Without that, "make SIX_TRACE_GUEST_SYSCALLS=0"
# after a traced build appears to succeed and produces a binary that is
# still tracing.
#
# It is a command-line variable when set, so make exports it to the
# sub-makes automatically.
#
SIX_TRACE_GUEST_SYSCALLS ?= 0
export SIX_TRACE_GUEST_SYSCALLS

SIX_STDFLAGS  = -std=gnu89 -fcommon -fno-strict-aliasing -fno-builtin -fno-pic
SIX_WARNFLAGS = -w

CFLAGS = -g -O0 $(SIX_STDFLAGS) $(SIX_WARNFLAGS) \
	 -DSIX_TRACE_GUEST_SYSCALLS=$(SIX_TRACE_GUEST_SYSCALLS)

ifdef CONFIG_CPP
CFLAGS := $(CFLAGS) -x c++
endif

ifdef SMP
CFLAGS += -D__SMP__
AFLAGS += -D__SMP__

ifdef SMP_PROF
CFLAGS += -D__SMP_PROF__
AFLAGS += -D__SMP_PROF__
endif
endif

#
# if you want the ram-disk device, define this to be the
# size in blocks.
#

#RAMDISK = -DRAMDISK=512

# Include the make variables (CC, etc...)
#

ARCHIVES	= kernel/kernel.o \
	       	  mm/mm.o \
		  fs/fs.o \
		  ipc/ipc.o \
		  net/network.o

ifdef SOLARIS_USER_MODE
# fs/Makefile rolls filesystems.o into fs.o for the SIX build, and ARCHIVES
# already links fs.o.  Naming it here as well would link it twice
# ("multiple definition of sys_setup").  The 2005 Makefile only avoided
# this by misspelling the variable as FILESSYSTEMS in the link rule, so it
# silently expanded to nothing.
FILESYSTEMS	=
else
FILESYSTEMS	=fs/filesystems.o 
endif
DRIVERS		=drivers/block/block.o \
		 drivers/char/char.o
LIBS		=$(TOPDIR)/lib/lib.o
#
# The guest userland (library/ = the emulated machine's mini-libc,
# applications/ = init, getty, login, sh, ls, ...) is built alongside the
# kernel and packaged into disk/x86/root by port/image/mkimage.sh.
# Each guest Makefile includes Rules.guest for its compiler/linker flags.
#
SUBDIRS		=kernel drivers mm fs net ipc lib library applications

ifeq ($(CONFIG_ISDN),y)
DRIVERS := $(DRIVERS) drivers/isdn/isdn.a
endif

ifdef SOLARIS_USER_MODE
DRIVERS := $(DRIVERS) drivers/net/net.o
else
DRIVERS := $(DRIVERS) drivers/net/net.a
endif

ifdef CONFIG_CD_NO_IDESCSI
DRIVERS := $(DRIVERS) drivers/cdrom/cdrom.a
endif

ifeq ($(CONFIG_SCSI),y)
DRIVERS := $(DRIVERS) drivers/scsi/scsi.a
endif

ifeq ($(CONFIG_SOUND),y)
DRIVERS := $(DRIVERS) drivers/sound/sound.a
endif

ifdef CONFIG_PCI
DRIVERS := $(DRIVERS) drivers/pci/pci.a
endif

ifdef CONFIG_SBUS
DRIVERS := $(DRIVERS) drivers/sbus/sbus.a
endif

#ifndef SOLARIS_USER_MODE
include ./arch/$(ARCH)/Makefile
#endif


ifndef SOLARIS_USER_MODE
ifdef SMP

.S.s:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -E -o $*.s $<
.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c -o $*.o $<

else

.S.s:
	$(CC) -D__ASSEMBLY__ -traditional -E -o $*.s $<
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c -o $*.o $<

endif
else
.c.s:
	$(CC) $(CFLAGS) -S -o $*.s $<
.s.o:
	$(AS) -P -I$(ROOT)/include -o $*.o $*
endif



Version: dummy
	@rm -f include/linux/compile.h

ifndef SOLARIS_USER_MODE
boot: vmlinux
	@$(MAKE) -C arch/$(ARCH)/boot

vmlinux: $(CONFIGURATION) init/main.o init/version.o linuxsubdirs
	$(LD) $(LINKFLAGS) $(HEAD) init/main.o init/version.o \
		$(ARCHIVES) \
		$(FILESYSTEMS) \
		$(DRIVERS) \
		$(LIBS) -o vmlinux
	$(NM) vmlinux | grep -v '\(compiled\)\|\(\.o$$\)\|\( a \)' | sort > System.map

symlinks:
	rm -f include/asm
	( cd include ; ln -sf asm-$(ARCH) asm)

oldconfig: symlinks
	$(CONFIG_SHELL) scripts/Configure -d arch/$(ARCH)/config.in

xconfig: symlinks
	$(MAKE) -C scripts kconfig.tk
	wish -f scripts/kconfig.tk

menuconfig: include/linux/version.h symlinks 
	$(MAKE) -C scripts/lxdialog all
	$(CONFIG_SHELL) scripts/Menuconfig arch/$(ARCH)/config.in

config: symlinks
	$(CONFIG_SHELL) scripts/Configure arch/$(ARCH)/config.in
else
symlinks:
	rm -f include/asm
	( cd include ; ln -sf asm-$(ARCH) asm)
config: symlinks
	@echo Configuration complete.
six:	linuxsubdirs init/version.o init/main.o
	$(CC) $(CFLAGS) -no-pie init/main.o init/version.o \
	-o $(INSTALL_PATH)/six \
	$(DRIVERS) \
	$(ARCHIVES) \
	$(FILESYSTEMS) \
	$(LIBS)

endif

#
# ---------------------------------------------------------------------------
# The guest root filesystem image
# ---------------------------------------------------------------------------
#
# disk/x86/root was a 5 MB ext2 blob checked into CVS in February 2005.  It
# has been deleted and untracked.  Nothing was lost: the pristine original,
# along with the big-endian SPARC one, is in git tag "v2005-cvs".
#
# It is now generated, because a checked-in image has three problems that
# only get worse with time:
#
#   1. Nothing tied its contents to the source tree sitting next to it.  The
#      /bin/sh inside it was built from some revision of applications/sh/ in
#      2003; which one, nobody can say.
#   2. Changing anything on it meant booting the kernel and using its own
#      built-in single-user shell (fs/single.c, "./six single") to copy files
#      in -- the author's own bootstrap tool, and a hard thing to repeat.
#   3. Every rebuild of a guest program silently did nothing, because the
#      image the kernel actually mounts was never updated.
#
# The image is described declaratively by port/image/manifest.txt and built
# by port/image/mkimage.sh (fakeroot + mke2fs -d, no privileges needed).
#
# IMAGE_FILES is the list of things the manifest copies in, extracted from
# the manifest itself so the two can never disagree.  It is wrapped in
# $(wildcard) so that binaries which do not exist yet -- the guest userland
# is still being ported -- are simply absent from the prerequisite list
# instead of making this an unbuildable target.  Once they exist, touching
# applications/ls/ls.c rebuilds ls, which makes ls newer than the image,
# which rebuilds the image.
#
# SIX_IMAGE itself is defined near the top of this file, next to do-it-all.
SIX_IMAGE_MANIFEST = port/image/manifest.txt
SIX_IMAGE_TOOL	= port/image/mkimage.sh
SIX_IMAGE_FILES	= $(shell sed 's/\#.*//' $(SIX_IMAGE_MANIFEST) | \
		    $(AWK) '$$1 == "file" { print $$4 }')

# The order-only dependency on "six" keeps the image from being assembled in
# parallel with the kernel link under make -j; the guest binaries are built
# by linuxsubdirs, which is a prerequisite of six.
$(SIX_IMAGE): $(SIX_IMAGE_MANIFEST) $(SIX_IMAGE_TOOL) $(SIX_IMAGE_FILES) | six
	$(CONFIG_SHELL) $(SIX_IMAGE_TOOL) --strict

.PHONY: image image-clean
image: $(SIX_IMAGE)

image-clean:
	rm -f $(SIX_IMAGE)
	rm -rf port/image/.stage


linuxsubdirs: dummy
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i; done

$(TOPDIR)/include/linux/version.h: include/linux/version.h
$(TOPDIR)/include/linux/compile.h: include/linux/compile.h

newversion:
	@if [ ! -f .version ]; then \
		echo 1 > .version; \
	else \
		expr 0`cat .version` + 1 > .version; \
	fi

include/linux/compile.h: $(CONFIGURATION) include/linux/version.h newversion
	@if [ -f .name ]; then \
	   echo \#define UTS_VERSION \"\#`cat .version`-`cat .name` `date`\"; \
	 else \
	   echo \#define UTS_VERSION \"\#`cat .version` `date`\";  \
	 fi >> .ver
	@echo \#define LINUX_COMPILE_TIME \"`date +%T`\" >> .ver
	@echo \#define LINUX_COMPILE_BY \"`whoami`\" >> .ver
	@echo \#define LINUX_COMPILE_HOST \"`hostname`\" >> .ver
	@if [ -x /bin/dnsdomainname ]; then \
	   echo \#define LINUX_COMPILE_DOMAIN \"`dnsdomainname`\"; \
	 elif [ -x /bin/domainname ]; then \
	   echo \#define LINUX_COMPILE_DOMAIN \"`domainname`\"; \
	 else \
	   echo \#define LINUX_COMPILE_DOMAIN ; \
	 fi >> .ver
	@echo \#define LINUX_COMPILER \"`$(CC) -v 2>&1 | tail -1`\" >> .ver
	@mv -f .ver $@

include/linux/version.h: ./Makefile
	@echo \#define UTS_RELEASE \"$(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\" > .ver
	@echo \#define LINUX_VERSION_CODE `expr $(VERSION) \\* 65536 + $(PATCHLEVEL) \\* 256 + $(SUBLEVEL)` >> .ver
	@mv -f .ver $@

init/version.o: init/version.c include/linux/compile.h
	$(CC) $(CFLAGS) -DUTS_MACHINE='"$(ARCH)"' -c -o init/version.o init/version.c

init/main.o: init/main.c
	$(CC) $(CFLAGS) $(PROFILING) -c -o $*.o $<

fs: dummy
	$(MAKE) linuxsubdirs SUBDIRS=fs

lib: dummy
	$(MAKE) linuxsubdirs SUBDIRS=lib

mm: dummy
	$(MAKE) linuxsubdirs SUBDIRS=mm

ipc: dummy
	$(MAKE) linuxsubdirs SUBDIRS=ipc

kernel: dummy
	$(MAKE) linuxsubdirs SUBDIRS=kernel

drivers: dummy
	$(MAKE) linuxsubdirs SUBDIRS=drivers

net: dummy
	$(MAKE) linuxsubdirs SUBDIRS=net

MODFLAGS = -DMODULE
ifdef CONFIG_MODULES
ifdef CONFIG_MODVERSIONS
MODFLAGS += -DMODVERSIONS -include $(HPATH)/linux/modversions.h
endif

modules: include/linux/version.h
	@set -e; \
	for i in $(SUBDIRS); \
	do $(MAKE) -C $$i CFLAGS="$(CFLAGS) $(MODFLAGS)" MAKING_MODULES=1 modules; \
	done

modules_install:
	@( \
	MODLIB=/lib/modules/$(VERSION).$(PATCHLEVEL).$(SUBLEVEL); \
	cd modules; \
	MODULES=""; \
	inst_mod() { These="`cat $$1`"; MODULES="$$MODULES $$These"; \
		mkdir -p $$MODLIB/$$2; cp -p $$These $$MODLIB/$$2; \
		echo Installing modules under $$MODLIB/$$2; \
	}; \
	\
	if [ -f BLOCK_MODULES ]; then inst_mod BLOCK_MODULES block; fi; \
	if [ -f NET_MODULES   ]; then inst_mod NET_MODULES   net;   fi; \
	if [ -f IPV4_MODULES  ]; then inst_mod IPV4_MODULES  ipv4;  fi; \
	if [ -f SCSI_MODULES  ]; then inst_mod SCSI_MODULES  scsi;  fi; \
	if [ -f FS_MODULES    ]; then inst_mod FS_MODULES    fs;    fi; \
	if [ -f CDROM_MODULES ]; then inst_mod CDROM_MODULES cdrom; fi; \
	\
	ls *.o > .allmods; \
	echo $$MODULES | tr ' ' '\n' | sort | comm -23 .allmods - > .misc; \
	if [ -s .misc ]; then inst_mod .misc misc; fi; \
	rm -f .misc .allmods; \
	)

# modules disabled....

else
modules modules_install: dummy
	@echo
	@echo "The present kernel configuration has modules disabled."
	@echo "Type 'make config' and enable loadable module support."
	@echo "Then build a kernel with module support enabled."
	@echo
	@exit 1
endif

ifdef SOLARIS_USER_MODE
clean:	image-clean
	$(MAKE) -C library clean
	$(MAKE) -C applications clean
	find . -name '*.[oa]' -not -path './CVS/*' -delete
	find . -name '.*.o.d' -delete
	rm -f $(ROOT)/six arch/six/kernel/.trace_flag .version include/linux/compile.h
else
clean:  archclean
        rm -f kernel/ksyms.lst include/linux/compile.h
        rm -f core `find . -name '*.[oas]' ! -regex '.*lxdialog/.*' -print`
        rm -f core `find . -type f -name 'core' -print`
        rm -f vmlinux System.map
        rm -f .tmp* drivers/sound/configure
        rm -fr modules/*
        rm -f submenu*
endif

mrproper: clean
	rm -f include/linux/autoconf.h include/linux/version.h
	rm -f drivers/sound/local.h drivers/sound/.defines
	rm -f drivers/scsi/aic7xxx_asm drivers/scsi/aic7xxx_seq.h
	rm -f drivers/char/uni_hash.tbl drivers/char/conmakehash
	rm -f .version .config* config.in config.old
	rm -f scripts/tkparse scripts/kconfig.tk scripts/kconfig.tmp
	rm -f scripts/lxdialog/*.o scripts/lxdialog/lxdialog
	rm -f .menuconfig .menuconfig.log
	rm -f include/asm
	rm -f .depend `find . -name .depend -print`
	rm -f .hdepend
	rm -f $(TOPDIR)/include/linux/modversions.h
	rm -f $(TOPDIR)/include/linux/modules/*


distclean: mrproper
	rm -f core `find . \( -name '*.orig' -o -name '*.rej' -o -name '*~' \
                -o -name '*.bak' -o -name '#*#' -o -name '.*.orig' \
                -o -name '.*.rej' -o -name '.SUMS' -o -size 0 \) -print` TAGS

backup: mrproper
	cd .. && tar cf - linux/ | gzip -9 > backup.gz
	sync

sums:
	find . -type f -print | sort | xargs sum > .SUMS

dep-files: archdep .hdepend include/linux/version.h
	$(AWK) -f scripts/depend.awk init/*.c > .tmpdepend
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i fastdep; done
	mv .tmpdepend .depend

MODVERFILE :=

ifdef CONFIG_MODVERSIONS
MODVERFILE := $(TOPDIR)/include/linux/modversions.h
endif

depend dep: dep-files $(MODVERFILE)

ifdef CONFIGURATION
..$(CONFIGURATION):
	@echo
	@echo "You have a bad or nonexistent" .$(CONFIGURATION) ": running 'make" $(CONFIGURATION)"'"
	@echo
	$(MAKE) $(CONFIGURATION)
	@echo
	@echo "Successful. Try re-making (ignore the error that follows)"
	@echo
	exit 1

#dummy: ..$(CONFIGURATION)
dummy:

else

dummy:

endif

include Rules.make

#
# This generates dependencies for the .h files.
#

.hdepend: dummy
	rm -f $@
	$(AWK) -f scripts/depend.awk `find $(HPATH) -name \*.h ! -name modversions.h -print` > .$@
	mv .$@ $@

vim_run:
	./six
