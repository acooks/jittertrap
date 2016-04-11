PROG = toptalk

SRC = main.c decode.c timeywimey.c
HEADERS = decode.h flow.h timeywimey.h

LIBS = -lpcap -lcurses

CFLAGS = -g -Wall -pedantic

all: $(SRC) $(HEADERS) Makefile
	$(CC) -o $(PROG) $(SRC) $(LIBS) $(CFLAGS)

cppcheck:
	cppcheck --enable=style,warning,performance,portability .
