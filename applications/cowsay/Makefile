include ../../Rules.guest

OBJS = cowsay.o

cowsay: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) cowsay
