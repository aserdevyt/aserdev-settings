CC = gcc
PKG_CFLAGS = $(shell pkg-config --cflags gtk4)
PKG_LIBS = $(shell pkg-config --libs gtk4)
CFLAGS = -Wall -Wextra -g $(PKG_CFLAGS)
LDFLAGS = $(PKG_LIBS)

TARGET = aser-settings

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) main.c -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
