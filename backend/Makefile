ifndef SAMPLE_PERIOD_US
SAMPLE_PERIOD_US = 10000
endif

ifndef WEB_SERVER_PORT
WEB_SERVER_PORT = 80
endif

ifndef WEB_SERVER_DOCUMENT_ROOT
WEB_SERVER_DOCUMENT_ROOT = /var/lib/jittertrap
endif

# list seperated by ':'
ifndef ALLOWED_IFACES
ALLOWED_IFACES = ""
endif

PROG = jittertrap
SOURCES = \
 jittertrap.c \
 stats_thread.c \
 sample_buf.c \
 netem.c \
 ../deps/fossa/fossa.c \

HEADERS = \
 jittertrap.h \
 netem.h \
 stats_thread.h \
 sample_buf.h \

INCLUDES = \
 -I ../deps/fossa/ \
 -I ../deps/fossa/deps/frozen

PKGCONFIG_LIBNL = \
 $$(pkg-config --cflags --libs libnl-3.0) \
 $$(pkg-config --cflags --libs libnl-route-3.0)

LFLAGS = -lrt -lm
CFLAGS = -W -Wall -std=c11 -g -pthread $(CFLAGS_EXTRA)
DEFINES = \
 -DSAMPLE_PERIOD_US=$(SAMPLE_PERIOD_US) \
 -DWEB_SERVER_PORT=$(WEB_SERVER_PORT) \
 -DWEB_SERVER_DOCUMENT_ROOT=$(WEB_SERVER_DOCUMENT_ROOT) \
 -DALLOWED_IFACES=$(ALLOWED_IFACES)

FOSSA_DEFINES = \
 -D NS_DISABLE_MQTT \
 -D NS_DISABLE_JSON_RPC \
 -D NS_DISABLE_MD5 \
 -D NS_DISABLE_HTTP_DIGEST_AUTH \


all: $(PROG)
	@echo -----------------------------------
	@echo Sample period: $(SAMPLE_PERIOD_US)us
	@echo Web server port: $(WEB_SERVER_PORT)
	@echo Web server document root: $(WEB_SERVER_DOCUMENT_ROOT)
	@echo Allowed Interfaces: $(ALLOWED_IFACES)
	@echo -----------------------------------

$(PROG): $(SOURCES) $(HEADERS)
	$(CC) $(SOURCES) $(INCLUDES) -o $@ $(CFLAGS) $(LFLAGS) $(PKGCONFIG_LIBNL) $(DEFINES) $(FOSSA_DEFINES)

clean:
	rm -rf *.o $(PROG)
