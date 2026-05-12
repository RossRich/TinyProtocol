PROJECT=small_protocol

CC     ?= gcc
CSTD   :=-std=c11
CFLAGS := -Wall -Wextra -W -Iinclude
TEST   := build/test_$(PROJECT)

SRCS := $(wildcard  src/*.c)
OBJS := $(patsubst src/%.c, build/obj/%.o, $(SRCS))
TESTSRC := tests/test_small_protocol.c

.PHONY: all test clean

all: test

build/obj/%.o: src/%.c | build/obj
	$(CC) $(CSTD) $(CFLAGS) -c $< -o $@

test: $(TEST)
	./$(TEST)

$(TEST): $(TESTSRC) $(OBJS) | build
	$(CC) $(CSTD) $(CFLAGS) $< $(OBJS) -o $@

build build/obj:
	mkdir -p $@

clean:
	$(RM) -r build/*
