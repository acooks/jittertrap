PROG = toptalk
LIB = toptalk.a
TEST = test-toptalk
TEST_RTT = test-tcp-rtt

SRC = \
 decode.c \
 intervals.c \
 intervals_user.c \
 tcp_rtt.c

HEADERS = \
 intervals.h \
 decode.h \
 flow.h \
 intervals.h \
 tcp_rtt.h

ifndef INTERVAL_COUNT
INTERVAL_COUNT = 8
endif

ifndef MAX_FLOW_COUNT
MAX_FLOW_COUNT = 100
endif

ifndef DEFINES
DEFINES += -DMAX_FLOW_COUNT=$(MAX_FLOW_COUNT)
DEFINES += -DINTERVAL_COUNT=$(INTERVAL_COUNT)
endif

PKGCONFIG_PCAP = $$(pkg-config --libs libpcap)
PKGCONFIG_CURSES = $$(pkg-config --cflags --libs ncursesw)

LDFLAGS := -lrt -lpthread \
 $(PKGCONFIG_PCAP) \
 $(PKGCONFIG_CURSES) \
 $(LDFLAGS)

CFLAGS_HARDENED = \
 -fPIC \
 -fstack-protector \
 --param ssp-buffer-size=4 \
 -fPIE -pie -Wl,-z,relro,-z,now

CFLAGS := -g -Wall -pedantic -std=c11 $(DEFINES) $(CFLAGS_HARDENED) $(CFLAGS)

.PHONY: all
all: $(LIB) $(TEST) $(TEST_RTT) $(PROG)

%.o: %.c %.h Makefile ../make.config make.config
	$(COMPILE.c) $(DEFINES) $< -o $@

$(PROG): $(LIB) $(SRC) $(HEADERS) Makefile main.c
	@echo Building $(PROG)
	$(CC) -o $(PROG) main.c timeywimey.c $(LIB) $(LDLIBS) $(LDFLAGS) $(CFLAGS)
	@echo -e "$(PROG) OK\n"

$(LIB): $(SRC) $(HEADERS) Makefile
	@echo Building $(LIB)
	$(CC) -c $(SRC) $(CFLAGS)
	gcc-ar cr $(LIB) *.o
	@echo -e "$(LIB) OK\n"

$(TEST): $(LIB) test.c
	@echo Building $(TEST)
	$(CC) -o $(TEST) test.c timeywimey.c $(LIB) $(LDLIBS) $(LDFLAGS) $(CFLAGS)

$(TEST_RTT): $(LIB) test_tcp_rtt.c
	@echo Building $(TEST_RTT)
	$(CC) -o $(TEST_RTT) test_tcp_rtt.c timeywimey.c $(LIB) $(LDLIBS) $(LDFLAGS) $(CFLAGS)

.PHONY: test
test: $(TEST) $(TEST_RTT)
	@echo "Running RTT unit tests (no root required)..."
	@./$(TEST_RTT) && echo -e "RTT unit tests OK\n"
	@echo "Running integration test (needs sudo for promiscuous network access)..."
	@sudo ./$(TEST) && echo -e "Integration test OK\n"

.PHONY: cppcheck
cppcheck:
	cppcheck --enable=warning,performance,portability --force .

.PHONY: clang-analyze
clang-analyze: clean
	scan-build -v make

.PHONY: clean
clean:
	rm $(LIB) $(PROG) $(TEST) $(TEST_RTT) *.o *.a || true
	rm *.gcno *.gcov *.gcda || true
