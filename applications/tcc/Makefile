include ../../Rules.guest

GUEST_CFLAGS += -I. -DTCC_TARGET_I386 -DONE_SOURCE=1 -DCONFIG_TCC_STATIC=1 -Wno-unused-result

OBJS = tcc.o

tcc: $(OBJS) $(GUEST_CRT) $(GUEST_LIBC)
	$(call guest_link,$@,$(OBJS))

clean:
	rm -f *.o tcc
