include ../../Rules.guest

OBJS = ttt.o

ttt: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) ttt
