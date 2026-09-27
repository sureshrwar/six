include ../../Rules.guest

passwd: passwd.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$<)

clean:
	rm -f *.o passwd
