.PHONY: all clean

CC = mpicc
CFLAGS = -g -Wall -lm

SRCS := $(wildcard *.c)
TARGET := $(patsubst %.c, %, $(SRCS))

all: $(TARGET)

.c:
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf $(TARGET)
