
PROG = jittertrap-cli

DEFINES =

SOURCES = \
 proto.c \
 main.c

HEADERS = \
 proto.h

INCLUDES = 

LFLAGS = -lwebsockets
CFLAGS = -W -Wall -std=c11 -g -pthread $(CFLAGS_EXTRA)

$(PROG): $(SOURCES) $(HEADERS)
	$(CC) $(SOURCES) $(INCLUDES) -o $@ $(CFLAGS) $(LFLAGS) $(DEFINES)
	

indent:
	clang-format -style=file -i $(SOURCES)

clean:
	rm $(PROG)
