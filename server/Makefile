
PROG = test-server

DEFINES = \
-DPROGNAME=\"$(PROG)\" \
-DINSTALL_DATADIR=\"./\"

SOURCES = \
 server-main.c \
 proto.c \
 proto-http.c \
 proto-jittertrap.c \

HEADERS = \
 proto.h \
 proto-http.h \
 proto-jittertrap.h \

INCLUDES = 

LFLAGS = -lwebsockets
CFLAGS = -W -Wall -std=c11 -g -pthread $(CFLAGS_EXTRA)

$(PROG): $(SOURCES) $(HEADERS)
	$(CC) $(SOURCES) $(INCLUDES) -o $@ $(CFLAGS) $(LFLAGS) $(DEFINES)
	

indent:
	clang-format -style=file -i $(SOURCES)

clean:
	rm $(PROG)
