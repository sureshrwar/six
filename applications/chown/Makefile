include ../../Rules.guest

chown: chown.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$<)

clean:
	rm -f *.o chown
