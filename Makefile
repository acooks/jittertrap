PROG = toptalk

SRC = main.c decode.c timeywimey.c intervals.c intervals_user.c
HEADERS = decode.h flow.h timeywimey.h intervals.h intervals_user.h

LIBS = -lpcap -lcurses -lrt -lpthread

CFLAGS = -g -Wall -pedantic

all: $(SRC) $(HEADERS) Makefile
	$(CC) -o $(PROG) $(SRC) $(LIBS) $(CFLAGS)

cppcheck:
	cppcheck --enable=style,warning,performance,portability .
