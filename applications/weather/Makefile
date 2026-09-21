include ../../Rules.guest

OBJS	=	weather.o

weather: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o weather
