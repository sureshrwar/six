include ../../Rules.guest

OBJS = servicemanager.o

servicemanager: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) servicemanager
