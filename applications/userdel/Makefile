include ../../Rules.guest

userdel: userdel.o $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$<)

clean:
	rm -f *.o userdel
