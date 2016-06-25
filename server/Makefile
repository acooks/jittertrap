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


SOURCES = \
 server-main.c \
 proto.c \
 proto-http.c \
 proto-jittertrap.c \
 mq_msg_ws.c \
 mq_msg_stats.c \
 jt_server_message_handler.c \
 timeywimey.c \
 sampling_thread.c \
 compute_thread.c \
 sample_buf.c \
 netem.c \
 slist.c \


HEADERS = \
 proto.h \
 proto-http.h \
 proto-jittertrap.h \
 mq_generic.h \
 mq_msg_ws.h \
 mq_msg_stats.h \
 jt_server_message_handler.h \
 netem.h \
 timeywimey.h \
 iface_stats.h \
 sampling_thread.h \
 compute_thread.h \
 sample_buf.h \
 slist.h \


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

test: test-mq test-mq-mt test-multi-mq
	./test-mq >/dev/null
	./test-mq-mt >/dev/null
	./test-multi-mq >/dev/null
	@echo -e "Test OK\n"


clean:
	rm $(PROG) || true
	rm test-mq test-mq-mt test-multi-mq || true
