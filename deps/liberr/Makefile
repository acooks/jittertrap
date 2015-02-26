CC = gcc
CFLAGS = -Wall -pedantic -std=c99
RM = rm

SRCS = err.c
OBJS = $(SRCS:.c=.o)
LIB_FILE = liberr.a

# for make install
HDR = err.h
INC_DIR = ../include 
LIB_DIR = ../lib

.PHONY: all

all: $(LIB_FILE) clean
	@echo Compiled library: liberr.a

$(LIB_FILE): $(OBJS)
	ar ruv $(LIB_FILE) $(OBJS)
	ranlib $(LIB_FILE)

%.o: %.c
	$(CC) $(CFLAGS) -c $< 

install:
	install -m 0644 $(LIB_FILE) $(LIB_DIR)
	install -m 0644 $(HDR) $(INC_DIR) 

clean:
	$(RM) *.o
