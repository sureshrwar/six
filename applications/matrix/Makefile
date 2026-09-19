include ../../Rules.guest

OBJS = matrix.o

matrix: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f $(OBJS) matrix
