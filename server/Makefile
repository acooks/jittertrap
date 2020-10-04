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
 -DMAX_JSON_MSG_LEN=$(MAX_JSON_MSG_LEN) \
 -DMAX_JSON_TOKEN_LEN=$(MAX_JSON_TOKEN_LEN) \
 -DMAX_FLOW_COUNT=$(MAX_FLOW_COUNT) \
 -DINTERVAL_COUNT=$(INTERVAL_COUNT) \
 -D_GNU_SOURCE \


SOURCES = \
 server-main.c \
 proto.c \
 proto-jittertrap.c \
 mq_msg_ws.c \
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


HEADERS = \
 proto.h \
 proto-jittertrap.h \
 mq_generic.h \
 mq_msg_ws.h \
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

OBJECTS += compute_thread.o
OBJECTS += server-main.o
OBJECTS += proto.o
OBJECTS += proto-jittertrap.o
OBJECTS += mq_msg_ws.o
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


MESSAGEHEADERS = \
 ../messages/include/jt_message_types.h \
 ../messages/include/jt_messages.h \
 ../messages/include/jt_msg_stats.h \
 ../messages/include/jt_msg_list_ifaces.h \
 ../messages/include/jt_msg_select_iface.h \
 ../messages/include/jt_msg_netem_params.h \
 ../messages/include/jt_msg_sample_period.h \
 ../messages/include/jt_msg_set_netem.h \

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

CFLAGS_HARDENED = \
 -fPIC \
 -fstack-protector \
 --param ssp-buffer-size=4 \
 -fPIE -pie -Wl,-z,relro,-z,now

CFLAGS :=  -W -Wall -pedantic -Wformat-security -std=c11 -g -pthread $(CFLAGS_HARDENED) $(INCLUDES) $(DEFINES) $(PKGCONFIG_LIBNL) $(CFLAGS)
LDFLAGS := -lwebsockets -ljansson -lm -lpcap -lrt $(LDFLAGS)

.PHONY: all
all: $(PROG) Makefile ../make.config
	@echo -----------------------------------
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
MQ_SOURCES = mq_msg_ws.c mq_msg_stats.c
MQ_HEADERS = mq_msg_ws.h mq_msg_stats.h
MQ_TEST_SOURCES = test_mq.c $(MQ_SOURCES)
MQ_MT_TEST_SOURCES = test_mq_mt.c $(MQ_SOURCES)
MQ_MULTI_TEST_SOURCES = test_multi_mq.c $(MQ_SOURCES)
MQ_TEST_HEADERS = mq_msg_ws.h mq_generic.h

test-mq: $(MQ_TEST_SOURCES) $(MQ_TEST_HEADERS) $(MQ_DEPENDS) $(MQ_HEADERS)
	$(CC) -o test-mq $(MQ_TEST_SOURCES) $(CFLAGS) -O0 $(DEFINES)

test-mq-mt: $(MQ_MT_TEST_SOURCES) $(MQ_TEST_HEADERS) $(MQ_HEADERS) $(MQ_DEPENDS)
	$(CC) -o test-mq-mt $(MQ_MT_TEST_SOURCES) $(CFLAGS) -O0 $(DEFINES)

test-multi-mq: $(MQ_MULTI_TEST_SOURCES) $(Q_TEST_HEADERS) $(MQ_HEADERS) $(MQ_DEPENDS)
	$(CC) -o test-multi-mq $(MQ_MULTI_TEST_SOURCES) $(CFLAGS) -O0 $(DEFINES)

test-slist: test_slist.c slist.o
	$(CC) -o test-slist test_slist.c slist.o

.PHONY: test
test: test-mq test-mq-mt test-multi-mq test-slist
	./test-mq >/dev/null
	./test-mq-mt >/dev/null
	./test-multi-mq >/dev/null
	./test-slist
	@echo -e "Test OK\n"

TOPTALK_TEST_SOURCES = test-toptalk.c timeywimey.c

test-toptalk: $(TOPTALK_LIB) $(TOPTALK_TEST_SOURCES)
	$(CC) -o test-toptalk $(TOPTALK_TEST_SOURCES) $(TOPTALK_LIB) $(INCLUDES) $(CFLAGS) -O0 $(DEFINES) -lpcap

.PHONY: clean
clean:
	rm $(PROG) *.o || true
	rm test-mq test-mq-mt test-multi-mq || true
