ifndef SAMPLE_PERIOD_MS
SAMPLE_PERIOD_MS = 10
endif

ifndef WEB_SERVER_PORT
WEB_SERVER_PORT = 8000
endif

ifndef WEB_SERVER_DOCUMENT_ROOT
WEB_SERVER_DOCUMENT_ROOT = /var/lib/jittertrap
endif

PROG = jittertrap
SOURCES = \
 jittertrap.c \
 stats_thread.c \
 netem.c \
 err.c \
 ../deps/fossa/fossa.c \

HEADERS = \
 netem.h \
 stats_thread.h \
 err.h

INCLUDES = \
 -I ../deps/fossa/ \
 -I ../deps/fossa/deps/frozen

PKGCONFIG_LIBNL = \
 $$(pkg-config --cflags --libs libnl-3.0) \
 $$(pkg-config --cflags --libs libnl-route-3.0)

LFLAGS = -lrt
CFLAGS = -W -Wall -std=c11 -g -pthread $(CFLAGS_EXTRA)
DEFINES = \
 -DSAMPLE_PERIOD_MS=$(SAMPLE_PERIOD_MS) \
 -DWEB_SERVER_PORT=$(WEB_SERVER_PORT) \
 -DWEB_SERVER_DOCUMENT_ROOT=$(WEB_SERVER_DOCUMENT_ROOT)

all: $(PROG)
	@echo -----------------------------------
	@echo Sample period: $(SAMPLE_PERIOD_MS)ms
	@echo Web server port: $(WEB_SERVER_PORT)
	@echo Web server document root: $(WEB_SERVER_DOCUMENT_ROOT)
	@echo -----------------------------------

$(PROG): $(SOURCES) $(HEADERS)
	$(CC) $(SOURCES) $(INCLUDES) -o $@ $(CFLAGS) $(LFLAGS) $(PKGCONFIG_LIBNL) $(DEFINES)

clean:
	rm -rf *.o $(PROG)
