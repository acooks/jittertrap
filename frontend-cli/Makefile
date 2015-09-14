
PROG = jittertrap-cli

DEFINES = -DMAX_IFACE_LEN=25

SOURCES = \
 jt_messages.c \
 proto.c \
 main.c \
 messages/jt_msg_stats.c \
 messages/jt_msg_list_ifaces.c \
 messages/jt_msg_select_iface.c \
 messages/jt_msg_netem_params.c \

HEADERS = \
 jt_message_types.h \
 jt_messages.h \
 proto.h \
 messages/jt_msg_stats.h \
 messages/jt_msg_list_ifaces.h \
 messages/jt_msg_select_iface.h \
 messages/jt_msg_netem_params.h \

INCLUDES = \
 -I . \
 -I messages \

LFLAGS = -lwebsockets -ljansson
CFLAGS = -W -Wall -pedantic -std=c11 -g -pthread $(CFLAGS_EXTRA)

$(PROG): $(SOURCES) $(HEADERS)
	$(CC) $(SOURCES) $(INCLUDES) -o $@ $(CFLAGS) $(LFLAGS) $(DEFINES)
	

indent:
	clang-format -style=file -i $(SOURCES) $(HEADERS)

clean:
	rm $(PROG) || true
