
PROG = jittertrap-cli

DEFINES = -DMAX_IFACE_LEN=25

SOURCES = \
 jt_messages.c \
 main.c \
 proto.c \

HEADERS = \
 proto.h \

MESSAGEHEADERS = \
 ../messages/include/jt_message_types.h \
 ../messages/include/jt_messages.h \
 ../messages/include/jt_msg_stats.h \
 ../messages/include/jt_msg_list_ifaces.h \
 ../messages/include/jt_msg_select_iface.h \
 ../messages/include/jt_msg_netem_params.h \
 ../messages/include/jt_msg_sample_period.h \
 ../messages/include/jt_msg_set_netem.h \

INCLUDES = \
 -I . \
 -I ../messages/include/ \

LFLAGS = -lwebsockets -ljansson
CFLAGS = -W -Wall -pedantic -std=c11 -g -pthread $(CFLAGS_EXTRA)

MESSAGES = ../messages/jt-messages.a

$(PROG): $(SOURCES) $(HEADERS) $(MESSAGES) $(MESSAGEHEADERS)
	$(CC) $(SOURCES) $(INCLUDES) $(MESSAGES) -o $@ $(CFLAGS) $(LFLAGS) $(DEFINES)


indent:
	clang-format -style=file -i $(SOURCES) $(HEADERS)


clean:
	rm $(PROG) || true
