include ../../Rules.guest

sarthak: sarthak.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$<)

clean:
	rm -f *.o sarthak
