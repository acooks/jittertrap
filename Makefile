
PROG = test-server

DEFINES = \
-DPROGNAME=\"$(PROG)\" \
-DINSTALL_DATADIR=\"./\"

SOURCES = \
 server-main.c \
 test.c \
 proto-http.c \
 proto-mirror.c \
 proto-jittertrap.c \
 proto-dinc.c 

HEADERS = \
 test.h \
 proto-http.h \
 proto-mirror.h \
 proto-jittertrap.h \
 proto-dinc.h \

INCLUDES = 

LFLAGS = -lwebsockets
CFLAGS = -W -Wall -std=c11 -g -pthread $(CFLAGS_EXTRA)

$(PROG): $(SOURCES) $(HEADERS)
	$(CC) $(SOURCES) $(INCLUDES) -o $@ $(CFLAGS) $(LFLAGS) $(DEFINES)
	

indent:
	clang-format -style=file -i $(SOURCES)

clean:
	rm $(PROG)
