include ../../Rules.guest

OBJS = nvme.o

nvme: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) nvme
