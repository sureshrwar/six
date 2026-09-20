#
# Mainframe Zork (Dungeon 3.2b) for SIX
#

include ../../Rules.guest

GUEST_CFLAGS += -DTEXTFILE='"/usr/lib/zork/dtextc.dat"' -DMORE_24

OBJS =	actors.o ballop.o clockr.o demons.o dgame.o dinit.o dmain.o \
	dso1.o dso2.o dso3.o dso4.o dso5.o dso6.o dso7.o dsub.o dverb1.o \
	dverb2.o gdt.o lightp.o local.o nobjs.o np.o np1.o np2.o np3.o \
	nrooms.o objcts.o rooms.o sobjs.o supp.o sverbs.o verbs.o villns.o

zork: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o zork
