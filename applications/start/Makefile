#
# cstart.o -- the guest entry point.
#
# Every guest program is linked with "-e cstart", so this is where a guest
# actually starts executing after SIX's ELF loader hands it control.
#
# Flags come from ../../Rules.guest.
#

include ../../Rules.guest

all: cstart.o

clean:
	rm -f *.o
