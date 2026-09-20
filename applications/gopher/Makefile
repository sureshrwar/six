include ../../Rules.guest

OBJS = gopher.o

gopher: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o gopher
