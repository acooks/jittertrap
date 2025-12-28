include ../make.config
include make.config

PROG = jt-server

DEFINES = \
 -DPROGNAME=\"$(PROG)\" \
 -DINSTALL_DATADIR=\"./\" \
 -DMAX_IFACE_LEN=$(MAX_IFACE_LEN) \
 -DSAMPLE_PERIOD_US=$(SAMPLE_PERIOD_US) \
 -DWEB_SERVER_PORT=$(WEB_SERVER_PORT) \
 -DWEB_SERVER_DOCUMENT_ROOT=$(WEB_SERVER_DOCUMENT_ROOT) \
 -DALLOWED_IFACES=$(ALLOWED_IFACES) \
 -DRT_CPU=$(RT_CPU) \
 -DRT_CPU_TOPTALK=$(RT_CPU_TOPTALK) \
 -DMAX_JSON_MSG_LEN=$(MAX_JSON_MSG_LEN) \
 -DMAX_JSON_TOKEN_LEN=$(MAX_JSON_TOKEN_LEN) \
 -DMAX_FLOW_COUNT=$(MAX_FLOW_COUNT) \
 -DINTERVAL_COUNT=$(INTERVAL_COUNT) \
 -D_GNU_SOURCE \

ifeq ($(DISABLE_IMPAIRMENTS), 1)
DEFINES += -DDISABLE_IMPAIRMENTS
endif

ifeq ($(DISABLE_PCAP), 1)
DEFINES += -DDISABLE_PCAP
endif

# WebRTC video playback support (requires libdatachannel)
ifeq ($(ENABLE_WEBRTC_PLAYBACK), 1)
DEFINES += -DENABLE_WEBRTC_PLAYBACK
LIBDATACHANNEL_CFLAGS = -I/usr/include/rtc
LIBDATACHANNEL_LDFLAGS = -ldatachannel
else
LIBDATACHANNEL_CFLAGS =
LIBDATACHANNEL_LDFLAGS =
endif

SOURCES = \
 server-main.c \
 proto.c \
 proto-jittertrap.c \
 mq_msg_ws_1.c \
 mq_msg_ws_2.c \
 mq_msg_ws_3.c \
 mq_msg_ws_4.c \
 mq_msg_ws_5.c \
 mq_msg_stats.c \
 mq_msg_tt.c \
 jt_server_message_handler.c \
 timeywimey.c \
 sampling_thread.c \
 compute_thread.c \
 tt_thread.c \
 sample_buf.c \
 netem.c \
 slist.c \
 intervals_user.c \
 pcap_buffer.c \
 capabilities.c \
 ws_compress.c \
 webrtc_bridge.c \


HEADERS = \
 proto.h \
 proto-jittertrap.h \
 mq_generic.h \
 mq_ws_tiered.h \
 mq_msg_ws_1.h \
 mq_msg_ws_2.h \
 mq_msg_ws_3.h \
 mq_msg_ws_4.h \
 mq_msg_ws_5.h \
 mq_msg_stats.h \
 mq_msg_tt.h \
 jt_server_message_handler.h \
 netem.h \
 timeywimey.h \
 iface_stats.h \
 sampling_thread.h \
 compute_thread.h \
 tt_thread.h \
 sample_buf.h \
 slist.h \
 pcap_buffer.h \
 capabilities.h \
 ws_compress.h \
 webrtc_bridge.h \

OBJECTS += compute_thread.o
OBJECTS += server-main.o
OBJECTS += proto.o
OBJECTS += proto-jittertrap.o
OBJECTS += mq_msg_ws_1.o
OBJECTS += mq_msg_ws_2.o
OBJECTS += mq_msg_ws_3.o
OBJECTS += mq_msg_ws_4.o
OBJECTS += mq_msg_ws_5.o
OBJECTS += mq_msg_stats.o
OBJECTS += mq_msg_tt.o
OBJECTS += jt_server_message_handler.o
OBJECTS += timeywimey.o
OBJECTS += sampling_thread.o
OBJECTS += tt_thread.o
OBJECTS += sample_buf.o
OBJECTS += netem.o
OBJECTS += slist.o
OBJECTS += intervals_user.o
OBJECTS += pcap_buffer.o
OBJECTS += capabilities.o
OBJECTS += ws_compress.o
OBJECTS += webrtc_bridge.o


MESSAGEHEADERS = \
 ../messages/include/jt_message_types.h \
 ../messages/include/jt_messages.h \
 ../messages/include/jt_msg_stats.h \
 ../messages/include/jt_msg_list_ifaces.h \
 ../messages/include/jt_msg_select_iface.h \
 ../messages/include/jt_msg_netem_params.h \
 ../messages/include/jt_msg_sample_period.h \
 ../messages/include/jt_msg_set_netem.h \
 ../messages/include/jt_msg_pcap.h \
 ../messages/include/jt_msg_video.h \

MAKEDEPENDS = Makefile ../make.config $(MESSAGEHEADERS)

INCLUDES = \
 -I . \
 -I ../messages/include/ \
 -I ../deps/toptalk/ \

PKGCONFIG_LIBNL = \
 $$(pkg-config --cflags --libs libnl-3.0) \
 $$(pkg-config --cflags --libs libnl-route-3.0)

TOPTALK_LIB = ../deps/toptalk/toptalk.a
MESSAGES = ../messages/jt-messages.a

# Build type: debug (default) or release
# debug: ASan enabled, -Og optimization, better for development
# release: -O2 optimization, FORTIFY_SOURCE, better for production
BUILD_TYPE ?= debug

CFLAGS_HARDENED = \
 -fPIC \
 -fstack-protector \
 --param ssp-buffer-size=4 \
 -fPIE -pie -Wl,-z,relro,-z,now \
 -Warray-bounds \
 -Wstringop-overflow

ifeq ($(BUILD_TYPE),release)
# Release build: full optimization with FORTIFY_SOURCE
CFLAGS_OPT = -O2 -D_FORTIFY_SOURCE=2
CFLAGS_SANITIZER =
LDFLAGS_SANITIZER =
else
# Debug build: minimal optimization with ASan for catching bugs early
CFLAGS_OPT = -Og
CFLAGS_SANITIZER = -fsanitize=address -fno-omit-frame-pointer
LDFLAGS_SANITIZER = -fsanitize=address
endif

CFLAGS :=  -W -Wall -pedantic -Wformat-security -std=c11 -g $(CFLAGS_OPT) -pthread $(CFLAGS_HARDENED) $(CFLAGS_SANITIZER) $(INCLUDES) $(DEFINES) $(PKGCONFIG_LIBNL) $(LIBDATACHANNEL_CFLAGS) $(CFLAGS)
LDFLAGS := -lwebsockets -ljansson -lm -lpcap -lrt -lcap -lz $(LDFLAGS_SANITIZER) $(LIBDATACHANNEL_LDFLAGS) $(LDFLAGS)

.PHONY: all
all: $(PROG) Makefile ../make.config
	@echo -----------------------------------
	@echo Build type: $(BUILD_TYPE)
	@echo Sample period: $(SAMPLE_PERIOD_US)us
	@echo Web server port: $(WEB_SERVER_PORT)
	@echo Web server document root: $(WEB_SERVER_DOCUMENT_ROOT)
	@echo Allowed Interfaces: $(ALLOWED_IFACES)
	@echo -----------------------------------

%.o: %.c %.h Makefile ../make.config make.config
	$(COMPILE.c) $(DEFINES) $< -o $@

$(PROG): $(OBJECTS) $(TOPTALK_LIB) $(MESSAGES)
	$(CC) $(OBJECTS) $(INCLUDES) $(TOPTALK_LIB) $(MESSAGES) -o $(PROG) $(CFLAGS) $(LDFLAGS) $(DEFINES) $(PKGCONFIG_LIBNL)


.PHONY: indent
indent:
	clang-format -style=file -i $(SOURCES) $(HEADERS)


MQ_DEPENDS = mq_generic.c mq_generic.h
MQ_WS_TIERED_SOURCES = mq_msg_ws_1.c mq_msg_ws_2.c mq_msg_ws_3.c mq_msg_ws_4.c mq_msg_ws_5.c
MQ_WS_TIERED_HEADERS = mq_ws_tiered.h mq_msg_ws_1.h mq_msg_ws_2.h mq_msg_ws_3.h mq_msg_ws_4.h mq_msg_ws_5.h
MQ_SOURCES = $(MQ_WS_TIERED_SOURCES) mq_msg_stats.c
MQ_HEADERS = $(MQ_WS_TIERED_HEADERS) mq_msg_stats.h
MQ_TEST_SOURCES = test_mq.c $(MQ_SOURCES)
MQ_MT_TEST_SOURCES = test_mq_mt.c $(MQ_SOURCES)
MQ_MULTI_TEST_SOURCES = test_multi_mq.c $(MQ_SOURCES)
MQ_TEST_HEADERS = $(MQ_WS_TIERED_HEADERS) mq_generic.h

test-mq: $(MQ_TEST_SOURCES) $(MQ_TEST_HEADERS) $(MQ_DEPENDS) $(MQ_HEADERS)
	$(CC) -o test-mq $(MQ_TEST_SOURCES) $(CFLAGS) -O0 $(DEFINES)

test-mq-mt: $(MQ_MT_TEST_SOURCES) $(MQ_TEST_HEADERS) $(MQ_HEADERS) $(MQ_DEPENDS)
	$(CC) -o test-mq-mt $(MQ_MT_TEST_SOURCES) $(CFLAGS) -O0 $(DEFINES)

test-multi-mq: $(MQ_MULTI_TEST_SOURCES) $(Q_TEST_HEADERS) $(MQ_HEADERS) $(MQ_DEPENDS)
	$(CC) -o test-multi-mq $(MQ_MULTI_TEST_SOURCES) $(CFLAGS) -O0 $(DEFINES)

test-slist: test_slist.c slist.o
	$(CC) -o test-slist test_slist.c slist.o $(CFLAGS) -O0 $(DEFINES)

test-pcap: test_pcap_buffer.c pcap_buffer.o
	$(CC) -o test-pcap test_pcap_buffer.c pcap_buffer.o $(CFLAGS) -O0 $(DEFINES) -lpcap -lpthread

.PHONY: test
test: test-mq test-mq-mt test-multi-mq test-slist test-pcap
	./test-mq >/dev/null
	./test-mq-mt
	./test-multi-mq >/dev/null
	./test-slist
	./test-pcap
	@echo -e "Test OK\n"

# Performance benchmark target - builds with optimization and SPEED_TEST
# Uses high stale threshold (1M) so consumers don't get marked stale during benchmark
bench-mq-mt: $(MQ_MT_TEST_SOURCES) $(MQ_TEST_HEADERS) $(MQ_HEADERS) $(MQ_DEPENDS)
	$(CC) -o bench-mq-mt $(MQ_MT_TEST_SOURCES) $(CFLAGS) -O2 -DSPEED_TEST -DMAX_DROPPED_BEFORE_STALE=1000000 $(DEFINES)

.PHONY: benchmark
benchmark: bench-mq-mt
	@echo "Running message queue benchmark (1M iterations, no sleeps)..."
	./bench-mq-mt

TOPTALK_TEST_SOURCES = test-toptalk.c timeywimey.c

test-toptalk: $(TOPTALK_LIB) $(TOPTALK_TEST_SOURCES)
	$(CC) -o test-toptalk $(TOPTALK_TEST_SOURCES) $(TOPTALK_LIB) $(INCLUDES) $(CFLAGS) -O0 $(DEFINES) -lpcap

# AddressSanitizer test targets
# These rebuild all tests with ASan instrumentation
# Note: UBSan (-fsanitize=undefined) can be added if libubsan is available
SANITIZER_FLAGS = -fsanitize=address -fno-omit-frame-pointer -g -O1

.PHONY: test-asan
test-asan: clean
	@echo "Building and running tests with AddressSanitizer..."
	$(MAKE) CFLAGS="$(CFLAGS) $(SANITIZER_FLAGS)" LDFLAGS="$(LDFLAGS) $(SANITIZER_FLAGS)" \
		test-mq test-mq-mt test-multi-mq test-slist test-pcap
	ASAN_OPTIONS=detect_leaks=0 ./test-mq >/dev/null
	ASAN_OPTIONS=detect_leaks=0 ./test-mq-mt
	ASAN_OPTIONS=detect_leaks=0 ./test-multi-mq >/dev/null
	ASAN_OPTIONS=detect_leaks=0 ./test-slist
	ASAN_OPTIONS=detect_leaks=0 ./test-pcap
	@echo "ASan tests passed!"

.PHONY: clean
clean:
	rm $(PROG) *.o || true
	rm test-mq test-mq-mt test-multi-mq test-pcap bench-mq-mt || true
	rm *.gcno *.gcov *.gcda || true
