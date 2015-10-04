include ../make.config

PROG = jt-server

DEFINES = \
 -DPROGNAME=\"$(PROG)\" \
 -DINSTALL_DATADIR=\"./\" \
 -DMAX_IFACE_LEN=$(MAX_IFACE_LEN) \
 -DSAMPLE_PERIOD_US=$(SAMPLE_PERIOD_US) \
 -DWEB_SERVER_PORT=$(WEB_SERVER_PORT) \
 -DWEB_SERVER_DOCUMENT_ROOT=$(WEB_SERVER_DOCUMENT_ROOT) \
 -DALLOWED_IFACES=$(ALLOWED_IFACES) \


SOURCES = \
 server-main.c \
 proto.c \
 proto-http.c \
 proto-jittertrap.c \
 jt_ws_mq.c \
 jt_server_message_handler.c \
 timeywimey.c \
 stats_thread.c \
 sample_buf.c \
 netem.c \


HEADERS = \
 proto.h \
 proto-http.h \
 proto-jittertrap.h \
 jt_ws_mq_config.h \
 jt_ws_mq.h \
 jt_server_message_handler.h \
 netem.h \
 timeywimey.h \
 iface_stats.h \
 stats_thread.h \
 sample_buf.h \


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
 -I ../backend/ \

PKGCONFIG_LIBNL = \
 $$(pkg-config --cflags --libs libnl-3.0) \
 $$(pkg-config --cflags --libs libnl-route-3.0)

MESSAGES = ../messages/jt-messages.a

LFLAGS = -lwebsockets -ljansson -lm
CFLAGS = -W -Wall -std=c11 -g -pthread $(CFLAGS_EXTRA)


all: $(PROG) Makefile ../make.config
	@echo -----------------------------------
	@echo Sample period: $(SAMPLE_PERIOD_US)us
	@echo Web server port: $(WEB_SERVER_PORT)
	@echo Web server document root: $(WEB_SERVER_DOCUMENT_ROOT)
	@echo Allowed Interfaces: $(ALLOWED_IFACES)
	@echo -----------------------------------

$(PROG): $(SOURCES) $(HEADERS) $(MESSAGES) $(MAKEDEPENDS)
	$(CC) $(SOURCES) $(INCLUDES) $(MESSAGES) -o $@ $(CFLAGS) $(LFLAGS) $(DEFINES) $(PKGCONFIG_LIBNL)


indent:
	clang-format -style=file -i $(SOURCES) $(HEADERS)


MQ_TEST_SOURCES = test_mq.c jt_ws_mq.c
MQ_MT_TEST_SOURCES = test_mq_mt.c jt_ws_mq.c
MQ_TEST_HEADERS = jt_ws_mq_config.h jt_ws_mq.h

test-mq: $(MQ_TEST_SOURCES) $(MQ_TEST_HEADERS)
	$(CC) -o test-mq $(MQ_TEST_SOURCES) $(CFLAGS) -O0

test-mq-mt: $(MQ_MT_TEST_SOURCES) $(MQ_TEST_HEADERS)
	$(CC) -o test-mq-mt $(MQ_MT_TEST_SOURCES) $(CFLAGS) -O0

test: test-mq test-mq-mt


clean:
	rm $(PROG) || true
