include ../make.config

PROG = test-server

DEFINES = \
-DPROGNAME=\"$(PROG)\" \
-DINSTALL_DATADIR=\"./\" \
-DMAX_IFACE_LEN=$(MAX_IFACE_LEN)

SOURCES = \
 server-main.c \
 proto.c \
 proto-http.c \
 proto-jittertrap.c \

HEADERS = \
 proto.h \
 proto-http.h \
 proto-jittertrap.h \

INCLUDES = 

LFLAGS = -lwebsockets
CFLAGS = -W -Wall -std=c11 -g -pthread $(CFLAGS_EXTRA)

$(PROG): $(SOURCES) $(HEADERS)
	$(CC) $(SOURCES) $(INCLUDES) -o $@ $(CFLAGS) $(LFLAGS) $(DEFINES)
	

indent:
	clang-format -style=file -i $(SOURCES)


MQ_TEST_SOURCES = test_mq.c websocket_message_queue.c
MQ_TEST_HEADERS = websocket_message_queue.h

test-mq: $(MQ_TEST_SOURCES) $(MQ_TEST_HEADERS)
	$(CC) -g -o test-mq $(MQ_TEST_SOURCES) $(CFLAGS)

clean:
	rm $(PROG)
