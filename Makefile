PROG = toptalk

SRC = decode.c timeywimey.c intervals.c intervals_user.c
HEADERS = intervals.h decode.h flow.h timeywimey.h intervals.h intervals_user.h

LIBS = -lpcap -lcurses -lrt -lpthread

CFLAGS += -g -Wall -pedantic

all: main test

main: $(SRC) $(HEADERS) Makefile
	$(CC) -o $(PROG) main.c $(SRC) $(LIBS) $(CFLAGS)
	@echo Build OK

cppcheck:
	cppcheck --enable=warning,performance,portability .

clang-analyze:
	scan-build make .

test:
	$(CC) -o test-$(PROG) test.c $(SRC) $(LIBS) $(CFLAGS)
	@./test-$(PROG) && echo "Test OK"
