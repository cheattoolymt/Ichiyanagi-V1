# Copyright 2026 nyan<(nyan4)
# Licensed under the Apache License, Version 2.0

CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter
LDFLAGS ?=
LDLIBS  ?= -lm

SRC_DIR := src
BUILD_DIR := build
BIN := $(BUILD_DIR)/ichiyanagi

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all clean test install

all: $(BIN)

$(BIN): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)
	@echo "Built: $(BIN)"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -c $< -o $@

test: all
	@echo "Running tests..."
	@bash tests/run_tests.sh

install: all
	install -Dm755 $(BIN) $(DESTDIR)/usr/local/bin/ichiyanagi

clean:
	rm -rf $(BUILD_DIR)
	@echo "Cleaned."
