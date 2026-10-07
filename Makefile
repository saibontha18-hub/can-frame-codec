CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -Werror -std=c11 -pedantic
INCLUDES = -Isrc

SRCS = src/can_frame.c src/bit_timing.c
OBJS = $(SRCS:.c=.o)

.PHONY: all test clean

all: demo smoke_test full_test

demo: $(OBJS) app/main.o
	$(CC) $(CFLAGS) -o $@ $^

smoke_test: $(OBJS) tests/smoke_test.o
	$(CC) $(CFLAGS) -o $@ $^

full_test: $(OBJS) tests/full_test.o
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

test: smoke_test full_test
	./smoke_test
	./full_test

clean:
	rm -f $(OBJS) app/main.o tests/smoke_test.o tests/full_test.o \
		demo smoke_test full_test
