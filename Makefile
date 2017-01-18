CC = gcc
CFLAGS = -g -Wall
LDFLAGS = -pthread -lssl -lcrypto
INC = -I./include

OBJ = \
	obj/csapp.o \
	obj/ring_buffer.o

BIN = \
	bin/client \
	bin/server

.PRECIOUS: obj/%.o
obj/%.o: src/%.c include/%.h
	$(CC) -c $(CFLAGS) $(INC) -o $@ $<

bin/%: src/%.c $(OBJ)
	$(CC) $(CFLAGS) $(INC) -o $@ $^ $(LDFLAGS)

all: $(BIN)

.PHONY: clean
clean:
	rm -rf obj/ bin/ core.*
