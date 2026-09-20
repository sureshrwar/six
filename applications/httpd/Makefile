include ../../Rules.guest

OBJS	=	httpd.o

httpd: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o httpd
