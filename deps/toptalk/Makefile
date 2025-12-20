PROG = toptalk
LIB = toptalk.a
TEST = test-toptalk
TEST_RTT = test-tcp-rtt
TEST_WINDOW = test-tcp-window
TEST_FLOW = test-flow
TEST_FLOW_KEY = test-flow-key
TEST_VIDEO = test-video-detect
TEST_RTSP = test-rtsp-tap

SRC = \
 decode.c \
 intervals.c \
 intervals_user.c \
 rtsp_tap.c \
 tcp_rtt.c \
 tcp_window.c \
 video_detect.c \
 video_metrics.c

HEADERS = \
 intervals.h \
 decode.h \
 flow.h \
 intervals.h \
 rtsp_tap.h \
 tcp_flow_key.h \
 tcp_rtt.h \
 tcp_window.h \
 video_detect.h \
 video_metrics.h

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

# Build type: debug (default) or release
BUILD_TYPE ?= debug

CFLAGS_HARDENED = \
 -fPIC \
 -fstack-protector \
 --param ssp-buffer-size=4 \
 -fPIE -pie -Wl,-z,relro,-z,now \
 -Warray-bounds \
 -Wstringop-overflow

# Enable SSE4.2 for hardware-accelerated CRC32C hash function (if available)
# Falls back to software implementation on CPUs without SSE4.2
CFLAGS_SIMD = -msse4.2

ifeq ($(BUILD_TYPE),release)
CFLAGS_OPT = -O2 -D_FORTIFY_SOURCE=2
CFLAGS_SANITIZER =
LDFLAGS_SANITIZER =
else
CFLAGS_OPT = -Og
CFLAGS_SANITIZER = -fsanitize=address -fno-omit-frame-pointer
LDFLAGS_SANITIZER = -fsanitize=address
endif

LDFLAGS := -lrt -lpthread \
 $(PKGCONFIG_PCAP) \
 $(PKGCONFIG_CURSES) \
 $(LDFLAGS_SANITIZER) \
 $(LDFLAGS)

CFLAGS := -g $(CFLAGS_OPT) -Wall -pedantic -std=c11 $(DEFINES) $(CFLAGS_HARDENED) $(CFLAGS_SIMD) $(CFLAGS_SANITIZER) $(CFLAGS)

.PHONY: all
all: $(LIB) $(TEST) $(TEST_RTT) $(TEST_WINDOW) $(TEST_FLOW) $(TEST_FLOW_KEY) $(TEST_VIDEO) $(TEST_RTSP) $(PROG)

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

$(TEST_WINDOW): $(LIB) test_tcp_window.c
	@echo Building $(TEST_WINDOW)
	$(CC) -o $(TEST_WINDOW) test_tcp_window.c timeywimey.c $(LIB) $(LDLIBS) $(LDFLAGS) $(CFLAGS)

$(TEST_FLOW): $(LIB) test_flow.c
	@echo Building $(TEST_FLOW)
	$(CC) -o $(TEST_FLOW) test_flow.c $(LIB) $(LDLIBS) $(LDFLAGS) $(CFLAGS)

$(TEST_FLOW_KEY): $(LIB) test_tcp_flow_key.c
	@echo Building $(TEST_FLOW_KEY)
	$(CC) -o $(TEST_FLOW_KEY) test_tcp_flow_key.c $(LIB) $(LDLIBS) $(LDFLAGS) $(CFLAGS)

$(TEST_VIDEO): $(LIB) test_video_detect.c
	@echo Building $(TEST_VIDEO)
	$(CC) -o $(TEST_VIDEO) test_video_detect.c $(LIB) $(LDLIBS) $(LDFLAGS) $(CFLAGS)

$(TEST_RTSP): $(LIB) test_rtsp_tap.c
	@echo Building $(TEST_RTSP)
	$(CC) -o $(TEST_RTSP) test_rtsp_tap.c $(LIB) $(LDLIBS) $(LDFLAGS) $(CFLAGS)

.PHONY: test
test: $(TEST) $(TEST_RTT) $(TEST_WINDOW) $(TEST_FLOW) $(TEST_FLOW_KEY) $(TEST_VIDEO) $(TEST_RTSP)
	@echo "Running RTT unit tests (no root required)..."
	@./$(TEST_RTT) && echo -e "RTT unit tests OK\n"
	@echo "Running window unit tests (no root required)..."
	@./$(TEST_WINDOW) && echo -e "Window unit tests OK\n"
	@echo "Running flow unit tests (no root required)..."
	@./$(TEST_FLOW) && echo -e "Flow unit tests OK\n"
	@echo "Running flow key unit tests (no root required)..."
	@./$(TEST_FLOW_KEY) && echo -e "Flow key unit tests OK\n"
	@echo "Running video detection unit tests (no root required)..."
	@./$(TEST_VIDEO) && echo -e "Video detection unit tests OK\n"
	@echo "Running RTSP tap unit tests (no root required)..."
	@./$(TEST_RTSP) && echo -e "RTSP tap unit tests OK\n"
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
	rm $(LIB) $(PROG) $(TEST) $(TEST_RTT) $(TEST_WINDOW) $(TEST_FLOW) $(TEST_FLOW_KEY) $(TEST_VIDEO) $(TEST_RTSP) *.o *.a || true
	rm *.gcno *.gcov *.gcda || true
