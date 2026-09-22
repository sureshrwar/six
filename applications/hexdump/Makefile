include ../../Rules.guest

hexdump: hexdump.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$<)

clean:
	rm -f *.o hexdump
