CC ?= gcc
CFLAGS ?= -O3 -Wall
PKG_CONFIG ?= pkg-config

WAYLAND_FLAGS := $(shell $(PKG_CONFIG) --cflags --libs wayland-client cairo) -lm -lpthread

BIN_DIR := bin
SRC_DIR := src
TARGET := $(BIN_DIR)/susuwatari

SRCS := $(SRC_DIR)/main.c $(SRC_DIR)/wlr-layer-shell-unstable-v1-protocol.c $(SRC_DIR)/xdg-shell-protocol.c

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRCS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(SRCS) $(WAYLAND_FLAGS) -o $(TARGET)

install: all
	@mkdir -p $(HOME)/.local/bin
	cp -f $(BIN_DIR)/susuwatari $(HOME)/.local/bin/susuwatari
	cp -f $(BIN_DIR)/susuwatari-toggle $(HOME)/.local/bin/susuwatari-toggle
	chmod +x $(HOME)/.local/bin/susuwatari $(HOME)/.local/bin/susuwatari-toggle

clean:
	rm -f $(TARGET)
