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

# Build type: debug (default) or release
BUILD_TYPE ?= debug

CFLAGS_HARDENED = \
 -fPIC \
 -fstack-protector \
 --param ssp-buffer-size=4 \
 -fPIE -pie -Wl,-z,relro,-z,now \
 -Warray-bounds \
 -Wstringop-overflow

ifeq ($(BUILD_TYPE),release)
CFLAGS_OPT = -O2 -D_FORTIFY_SOURCE=2
CFLAGS_SANITIZER =
LDFLAGS_SANITIZER =
else
CFLAGS_OPT = -Og
CFLAGS_SANITIZER = -fsanitize=address -fno-omit-frame-pointer
LDFLAGS_SANITIZER = -fsanitize=address
endif

LDLIBS := -lwebsockets -ljansson -lrt $(LDFLAGS_SANITIZER) $(LDLIBS)
CFLAGS := -W -Wall -pedantic -std=c11 -g $(CFLAGS_OPT) -pthread $(CFLAGS_HARDENED) $(CFLAGS_SANITIZER) $(CFLAGS)

MESSAGES = ../messages/jt-messages.a

$(PROG): $(SOURCES) $(HEADERS) $(MESSAGES) $(MAKEDEPENDS)
	$(CC) $(SOURCES) $(INCLUDES) $(MESSAGES) -o $@ $(CFLAGS) $(LDLIBS) $(LDFLAGS) $(DEFINES)


.PHONY: indent
indent:
	clang-format -style=file -i $(SOURCES) $(HEADERS)


.PHONY: clean
clean:
	rm $(PROG) || true
	rm *.gcno *.gcov *.gcda || true


.PHONY: test
test:
	true
