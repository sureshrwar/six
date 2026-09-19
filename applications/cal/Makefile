include ../../Rules.guest

OBJS = cal.o

cal: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) cal
