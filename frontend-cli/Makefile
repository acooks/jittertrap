
PROG = jittertrap-cli

DEFINES = -DMAX_IFACE_LEN=25

MAIN = main.c

SOURCES = \
 jt_messages.c \
 proto.c \
 messages/jt_msg_stats.c \
 messages/jt_msg_list_ifaces.c \
 messages/jt_msg_select_iface.c \
 messages/jt_msg_netem_params.c \
 messages/jt_msg_sample_period.c \
 messages/jt_msg_get_netem.c \
 messages/jt_msg_set_netem.c \

HEADERS = \
 jt_message_types.h \
 jt_messages.h \
 proto.h \
 messages/jt_msg_stats.h \
 messages/jt_msg_list_ifaces.h \
 messages/jt_msg_select_iface.h \
 messages/jt_msg_netem_params.h \
 messages/jt_msg_sample_period.h \
 messages/jt_msg_get_netem.h \
 messages/jt_msg_set_netem.h \

INCLUDES = \
 -I . \
 -I messages \

LFLAGS = -lwebsockets -ljansson
CFLAGS = -W -Wall -pedantic -std=c11 -g -pthread $(CFLAGS_EXTRA)

$(PROG): $(SOURCES) $(HEADERS) test
	$(CC) $(MAIN) $(SOURCES) $(INCLUDES) -o $@ $(CFLAGS) $(LFLAGS) $(DEFINES)
	

indent:
	clang-format -style=file -i $(SOURCES) $(HEADERS)


test: $(SOURCES) $(HEADERS) test_messages.c
	$(CC) test_messages.c $(SOURCES) $(INCLUDES) -o $@ $(CFLAGS) -O0 $(LFLAGS) $(DEFINES)


clean:
	rm $(PROG) || true
