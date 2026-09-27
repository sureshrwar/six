include ../../Rules.guest

su: su.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$<)

clean:
	rm -f *.o su
