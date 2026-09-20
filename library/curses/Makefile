include ../../Rules.guest

SRC	=	curses.c
OBJS	=	$(SRC:.c=.o)

TARGET	=	./lib/libcurses.a

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p ./lib
	$(GUEST_AR) -r $(TARGET) $(OBJS)

clean:
	rm -f *.o $(TARGET)
	rm -rf ./lib
