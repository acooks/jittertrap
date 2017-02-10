PROG = toptalk
LIB = toptalk.a
TEST = test-toptalk

SRC = \
 decode.c \
 intervals.c \
 intervals_user.c

HEADERS = \
 intervals.h \
 decode.h \
 flow.h \
 intervals.h \
 intervals_user.h

LFLAGS = -lpcap -lcurses -lrt -lpthread

CFLAGS_HARDENED = \
 -fPIC \
 -fstack-protector \
 --param ssp-buffer-size=4 \
 -fPIE -pie -Wl,-z,relro,-z,now

CFLAGS := -g -Wall -pedantic -std=c11 $(CFLAGS_HARDENED) $(CFLAGS)

.PHONY: all
all: $(LIB) $(TEST) $(PROG)

$(PROG): $(LIB) $(SRC) $(HEADERS) Makefile main.c
	@echo Building $(PROG)
	$(CC) -o $(PROG) main.c timeywimey.c $(LIB) $(LFLAGS) $(CFLAGS)
	@echo -e "$(PROG) OK\n"

$(LIB): $(SRC) $(HEADERS) Makefile
	@echo Building $(LIB)
	$(CC) -c $(SRC) $(CFLAGS)
	gcc-ar cr $(LIB) *.o
	@echo -e "$(LIB) OK\n"

$(TEST): $(LIB) test.c
	@echo Building $(TEST)
	$(CC) -o $(TEST) test.c timeywimey.c $(LIB) $(LFLAGS) $(CFLAGS)

.PHONY: test
test: $(TEST)
	@echo "Test needs sudo for promiscuous network access..."
	@sudo ./$(TEST) && echo -e "Test OK\n"

cppcheck:
	cppcheck --enable=warning,performance,portability --force .

clang-analyze: clean
	scan-build -v make

clean:
	rm $(LIB) $(PROG) $(TEST) *.o *.a || true
