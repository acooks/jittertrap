PROG = toptalk

SRC = \
 decode.c \
 timeywimey.c \
 intervals.c \
 intervals_user.c

HEADERS = \
 intervals.h \
 decode.h \
 flow.h \
 timeywimey.h \
 intervals.h \
 intervals_user.h

LFLAGS = -lpcap -lcurses -lrt -lpthread

CFLAGS += -g -Wall -pedantic

all: main test

main: $(SRC) $(HEADERS) Makefile
	$(CC) -o $(PROG) main.c $(SRC) $(LFLAGS) $(CFLAGS)
	@echo Build OK

cppcheck:
	cppcheck --enable=warning,performance,portability .

clang-analyze:
	scan-build make .

test:
	$(CC) -o test-$(PROG) test.c $(SRC) $(LFLAGS) $(CFLAGS)
	@./test-$(PROG) && echo "Test OK"
