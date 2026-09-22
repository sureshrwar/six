include ../../Rules.guest

OBJS	=	ext4info.o

ext4info: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o ext4info
