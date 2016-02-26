include ../make.config

LIB = jt-messages.a

DEFINES = -DMAX_IFACE_LEN=$(MAX_IFACE_LEN)

SOURCES = \
 src/jt_msg_stats.c \
 src/jt_msg_pgap_distr.c \
 src/jt_msg_list_ifaces.c \
 src/jt_msg_select_iface.c \
 src/jt_msg_netem_params.c \
 src/jt_msg_sample_period.c \
 src/jt_msg_set_netem.c \
 src/jt_messages.c \

HEADERS = \
 include/jt_message_types.h \
 include/jt_msg_stats.h \
 include/jt_msg_pgap_distr.h \
 include/jt_msg_list_ifaces.h \
 include/jt_msg_select_iface.h \
 include/jt_msg_netem_params.h \
 include/jt_msg_sample_period.h \
 include/jt_msg_set_netem.h \

INCLUDES = \
 -I . \
 -I include \

LFLAGS = -ljansson
CFLAGS = -W -Wall -pedantic -std=c11 -g $(CFLAGS_EXTRA)

$(LIB): compile lib test-packing Makefile


compile: $(SOURCES) $(HEADERS)
	$(CC) -c $(SOURCES) $(INCLUDES) $(CFLAGS) $(DEFINES)


lib: compile
	gcc-ar cr $(LIB) *.o


indent:
	clang-format -style=file -i $(SOURCES) $(HEADERS)


test-packing: lib test/test_messages.c
	$(CC) test/test_messages.c $(SOURCES) $(INCLUDES) -o $@ $(CFLAGS) -O0 $(LFLAGS) $(DEFINES)


clean:
	rm $(LIB) *.o *.a test-packing || true
