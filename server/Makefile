include ../make.config

PROG = test-server

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
 websocket_message_queue.c \
 jt_server_message_handler.c \
 ../backend/timeywimey.c \
 ../backend/stats_thread.c \
 ../backend/sample_buf.c \
 ../backend/netem.c \


HEADERS = \
 proto.h \
 proto-http.h \
 proto-jittertrap.h \
 websocket_message_queue.h \
 jt_server_message_handler.h \
 ../backend/netem.h \
 ../backend/timeywimey.h \
 ../backend/iface_stats.h \
 ../backend/stats_thread.h \
 ../backend/sample_buf.h \


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


$(PROG): $(SOURCES) $(HEADERS) $(MESSAGES) $(MAKEDEPENDS)
	$(CC) $(SOURCES) $(INCLUDES) $(MESSAGES) -o $@ $(CFLAGS) $(LFLAGS) $(DEFINES) $(PKGCONFIG_LIBNL)


indent:
	clang-format -style=file -i $(SOURCES) $(HEADERS)


MQ_TEST_SOURCES = test_mq.c websocket_message_queue.c
MQ_TEST_HEADERS = websocket_message_queue.h

test-mq: $(MQ_TEST_SOURCES) $(MQ_TEST_HEADERS)
	$(CC) -g -o test-mq $(MQ_TEST_SOURCES) $(CFLAGS)

clean:
	rm $(PROG) || true
