PROG = jittertrap
SOURCES = \
 jittertrap.c \
 stats_thread.c \
 netem.c \
 ../deps/fossa/fossa.c \

INCLUDES = \
 -I ../deps/fossa/ \
 -I ../deps/fossa/deps/frozen

PKGCONFIG_LIBNL = \
 $$(pkg-config --cflags --libs libnl-3.0) \
 $$(pkg-config --cflags --libs libnl-route-3.0)

LFLAGS = -lrt
CFLAGS = -W -Wall -std=c11 -g -pthread $(CFLAGS_EXTRA)

all: $(PROG)

$(PROG): $(SOURCES)
	$(CC) $(SOURCES) $(INCLUDES) -o $@ $(CFLAGS) $(LFLAGS) $(PKGCONFIG_LIBNL)

clean:
	rm -rf *.o $(PROG)
