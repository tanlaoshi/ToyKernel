# HostChat/Makefile — PR-CHAT-1
CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2 -std=c11
TARGET  := chatd

.PHONY: all clean

all: $(TARGET)

$(TARGET): chatd.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)
