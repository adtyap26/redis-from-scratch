# Makefile

COMPILER := gcc
BUILD_DIR := builds
CFLAGS := -Wall -Wextra -Og -g

SERVER_SRC := main.c
CLIENT_SRC := client.c

SERVER_BIN := $(BUILD_DIR)/server
CLIENT_BIN := $(BUILD_DIR)/client

.PHONY: all clean

all: clean build-dir $(SERVER_BIN) $(CLIENT_BIN)

build-dir:
	@mkdir -p $(BUILD_DIR)

clean:
	@echo "Cleaning $(BUILD_DIR)..."
	@rm -rf $(BUILD_DIR)/*

$(SERVER_BIN): $(SERVER_SRC)
	@echo "Building for Linux (64-bit)..."
	@$(COMPILER) $(CFLAGS) $< -o $@ && \
	echo "Build complete! server binary is in the builds directory." || \
	( echo "Compilation of $< failed!" && exit 1 )

$(CLIENT_BIN): $(CLIENT_SRC)
	@echo "Compiling $<..."
	@$(COMPILER) $(CFLAGS) $< -o $@ && \
	echo "Successfully built client." || \
	( echo "Compilation of $< failed!" && exit 1 )

