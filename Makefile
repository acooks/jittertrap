
PROG = jittertrap-cli

DEFINES = -DMAX_IFACE_LEN=25

SOURCES = \
 jt_messages.c \
 jt_msg_stats.c \
 jt_msg_list_ifaces.c \
 proto.c \
 main.c \

HEADERS = \
 jt_message_types.h \
 jt_messages.h \
 jt_msg_stats.h \
 jt_msg_list_ifaces.h \
 proto.h \

INCLUDES = 

LFLAGS = -lwebsockets -ljansson
CFLAGS = -W -Wall -pedantic -std=c11 -g -pthread $(CFLAGS_EXTRA)

$(PROG): $(SOURCES) $(HEADERS)
	$(CC) $(SOURCES) $(INCLUDES) -o $@ $(CFLAGS) $(LFLAGS) $(DEFINES)
	

indent:
	clang-format -style=file -i $(SOURCES) $(HEADERS)

clean:
	rm $(PROG)
