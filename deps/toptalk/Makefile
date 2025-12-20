PROG = toptalk
LIB = toptalk.a
TEST = test-toptalk
TEST_RTT = test-tcp-rtt
TEST_WINDOW = test-tcp-window
TEST_VIDEO = test-video-detect
TEST_RTSP = test-rtsp-tap

# Benchmark targets
BENCH_DECODE = bench-decode
BENCH_MALLOC = bench-malloc
BENCH_ROTATION = bench-rotation
BENCH_SORT = bench-sort

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
 tcp_rtt.h \
 tcp_window.h \
 video_detect.h \
 video_metrics.h \
 bench_common.h

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

CFLAGS := -g $(CFLAGS_OPT) -Wall -pedantic -std=c11 $(DEFINES) $(CFLAGS_HARDENED) $(CFLAGS_SANITIZER) $(CFLAGS)

.PHONY: all
all: $(LIB) $(TEST) $(TEST_RTT) $(TEST_WINDOW) $(TEST_VIDEO) $(TEST_RTSP) $(PROG)

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

$(TEST_VIDEO): $(LIB) test_video_detect.c
	@echo Building $(TEST_VIDEO)
	$(CC) -o $(TEST_VIDEO) test_video_detect.c $(LIB) $(LDLIBS) $(LDFLAGS) $(CFLAGS)

$(TEST_RTSP): $(LIB) test_rtsp_tap.c
	@echo Building $(TEST_RTSP)
	$(CC) -o $(TEST_RTSP) test_rtsp_tap.c $(LIB) $(LDLIBS) $(LDFLAGS) $(CFLAGS)

# Benchmark targets - built without sanitizers for accurate timing
# These need a clean library build without ASAN
BENCH_CFLAGS := -g -O2 -Wall -pedantic -std=c11 $(DEFINES) -fPIC -fno-omit-frame-pointer
BENCH_LDFLAGS := -lrt -lpthread $(PKGCONFIG_PCAP) $(PKGCONFIG_CURSES)

# Library built without sanitizers for benchmarks
BENCH_LIB = toptalk-bench.a

$(BENCH_LIB): $(SRC) $(HEADERS) Makefile
	@echo Building $(BENCH_LIB) without sanitizers
	$(CC) -c $(SRC) $(BENCH_CFLAGS)
	gcc-ar cr $(BENCH_LIB) *.o
	@echo -e "$(BENCH_LIB) OK\n"

$(BENCH_DECODE): $(BENCH_LIB) bench_decode.c bench_common.c bench_common.h
	@echo Building $(BENCH_DECODE)
	$(CC) -o $(BENCH_DECODE) bench_decode.c bench_common.c $(BENCH_LIB) $(LDLIBS) $(BENCH_LDFLAGS) $(BENCH_CFLAGS)

$(BENCH_MALLOC): $(BENCH_LIB) bench_malloc.c bench_common.c bench_common.h
	@echo Building $(BENCH_MALLOC)
	$(CC) -o $(BENCH_MALLOC) bench_malloc.c bench_common.c $(BENCH_LIB) $(LDLIBS) $(BENCH_LDFLAGS) $(BENCH_CFLAGS)

$(BENCH_ROTATION): $(BENCH_LIB) bench_rotation.c bench_common.c bench_common.h
	@echo Building $(BENCH_ROTATION)
	$(CC) -o $(BENCH_ROTATION) bench_rotation.c bench_common.c $(BENCH_LIB) $(LDLIBS) $(BENCH_LDFLAGS) $(BENCH_CFLAGS)

$(BENCH_SORT): $(BENCH_LIB) bench_sort.c bench_common.c bench_common.h
	@echo Building $(BENCH_SORT)
	$(CC) -o $(BENCH_SORT) bench_sort.c bench_common.c $(BENCH_LIB) $(LDLIBS) $(BENCH_LDFLAGS) $(BENCH_CFLAGS)

BENCH_REGRESSION = bench-regression

$(BENCH_REGRESSION): $(BENCH_LIB) bench_regression.c bench_common.c bench_common.h
	@echo Building $(BENCH_REGRESSION)
	$(CC) -o $(BENCH_REGRESSION) bench_regression.c bench_common.c $(BENCH_LIB) $(LDLIBS) $(BENCH_LDFLAGS) $(BENCH_CFLAGS)

.PHONY: bench
bench: $(BENCH_DECODE) $(BENCH_MALLOC) $(BENCH_ROTATION) $(BENCH_SORT)
	@echo "Running decode benchmark..."
	@./$(BENCH_DECODE)
	@echo "Running malloc benchmark..."
	@./$(BENCH_MALLOC)
	@echo "Running rotation benchmark..."
	@./$(BENCH_ROTATION)
	@echo "Running sort benchmark..."
	@./$(BENCH_SORT)

.PHONY: bench-test
bench-test: $(BENCH_REGRESSION)
	@echo "Running performance regression tests..."
	@./$(BENCH_REGRESSION)

.PHONY: test
test: $(TEST) $(TEST_RTT) $(TEST_WINDOW) $(TEST_VIDEO) $(TEST_RTSP)
	@echo "Running RTT unit tests (no root required)..."
	@./$(TEST_RTT) && echo -e "RTT unit tests OK\n"
	@echo "Running window unit tests (no root required)..."
	@./$(TEST_WINDOW) && echo -e "Window unit tests OK\n"
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
	rm $(LIB) $(PROG) $(TEST) $(TEST_RTT) $(TEST_WINDOW) $(TEST_VIDEO) $(TEST_RTSP) *.o *.a || true
	rm $(BENCH_DECODE) $(BENCH_MALLOC) $(BENCH_ROTATION) $(BENCH_SORT) $(BENCH_REGRESSION) || true
	rm *.gcno *.gcov *.gcda || true
