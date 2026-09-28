include ../../Rules.guest

OBJS = externalstoraged.o

externalstoraged: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) externalstoraged
