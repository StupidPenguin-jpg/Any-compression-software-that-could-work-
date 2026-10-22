CC = gcc
CFLAGS = -O2 -Wall `pkg-config --cflags gtk+-3.0`
LIBS = `pkg-config --libs gtk+-3.0` -lzip -larchive
TARGET = Huffman
SRCDIR = src
SRCS = $(SRCDIR)/main.c $(SRCDIR)/core.c $(SRCDIR)/gui.c
OBJS = $(SRCS:.c=.o)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: all clean
all: $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
