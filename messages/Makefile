include ../make.config

LIB = jt-messages.a

DEFINES = -DMAX_IFACE_LEN=$(MAX_IFACE_LEN)

SOURCES = \
 src/jt_msg_stats.c \
 src/jt_msg_toptalk.c \
 src/jt_msg_list_ifaces.c \
 src/jt_msg_select_iface.c \
 src/jt_msg_netem_params.c \
 src/jt_msg_sample_period.c \
 src/jt_msg_set_netem.c \
 src/jt_messages.c \

HEADERS = \
 include/jt_message_types.h \
 include/jt_msg_stats.h \
 include/jt_msg_toptalk.h \
 include/jt_msg_list_ifaces.h \
 include/jt_msg_select_iface.h \
 include/jt_msg_netem_params.h \
 include/jt_msg_sample_period.h \
 include/jt_msg_set_netem.h \

OBJECTS += jt_msg_stats.o
OBJECTS += jt_msg_toptalk.o
OBJECTS += jt_msg_list_ifaces.o
OBJECTS += jt_msg_select_iface.o
OBJECTS += jt_msg_netem_params.o
OBJECTS += jt_msg_sample_period.o
OBJECTS += jt_msg_set_netem.o
OBJECTS += jt_messages.o

INCLUDES = \
 -I . \
 -I include \
 -I ../deps/toptalk

LFLAGS = -ljansson
CFLAGS := -pthread $(INCLUDES) $(DEFINES) $(PKGCONFIG_LIBNL) $(CFLAGS)

.PHONY: all
all: $(LIB) test-packing

$(OBJECTS): %.o: src/%.c include/%.h Makefile ../make.config
	$(COMPILE.c) $< -o $@


$(LIB): $(OBJECTS) Makefile
	gcc-ar cr $(LIB) *.o


indent:
	clang-format -style=file -i $(SOURCES) $(HEADERS)


test-packing: $(LIB) test/test_messages.c
	$(CC) test/test_messages.c $(SOURCES) $(INCLUDES) -o $@ $(CFLAGS) -O0 $(LFLAGS) $(DEFINES)


test: test-packing
	@./test-packing >/dev/null && echo -e "Test OK\n"

clean:
	rm $(LIB) *.o *.a test-packing || true
