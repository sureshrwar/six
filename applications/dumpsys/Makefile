include ../../Rules.guest

OBJS = dumpsys.o

dumpsys: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) dumpsys
