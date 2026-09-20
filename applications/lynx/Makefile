include ../../Rules.guest

OBJS	=	lynx.o

lynx: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o lynx
