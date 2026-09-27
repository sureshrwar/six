include ../../Rules.guest

who: who.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$<)

clean:
	rm -f *.o who
