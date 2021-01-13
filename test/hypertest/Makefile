.PHONY: all clean

CC = mpicc
CFLAGS = -g -Wall

SRCS := $(wildcard *.c)
TARGET := $(patsubst %.c, %, $(SRCS))

all: $(TARGET)

.c:
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf $(TARGET)
