include ../make.config

PROG = jittertrap-cli

DEFINES = \
 -DMAX_IFACE_LEN=25 \
 -D_GNU_SOURCE \


SOURCES = \
 main.c \
 proto.c \
 jt_client_msg_handler.c \

HEADERS = \
 proto.h \
 jt_client_msg_handler.h \

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

LFLAGS = -lwebsockets -ljansson
CFLAGS := -W -Wall -pedantic -std=c11 -g -pthread $(CFLAGS)

MESSAGES = ../messages/jt-messages.a

$(PROG): $(SOURCES) $(HEADERS) $(MESSAGES) $(MAKEDEPENDS)
	$(CC) $(SOURCES) $(INCLUDES) $(MESSAGES) -o $@ $(CFLAGS) $(LFLAGS) $(DEFINES)


indent:
	clang-format -style=file -i $(SOURCES) $(HEADERS)


clean:
	rm $(PROG) || true

test:
	true
