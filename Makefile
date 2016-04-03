PROG = toptalk

SRC = main.c

LIBS = -lpcap

CFLAGS = -g -Wall

all: $(SRC)
	gcc -o $(PROG) $(SRC) $(LIBS) $(CFLAGS)
