include ../../Rules.guest

dmesg: dmesg.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$<)

clean:
	rm -f *.o dmesg
