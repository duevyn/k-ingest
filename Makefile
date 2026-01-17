CC      := gcc

# Your sources
SRCS    := server.c rbuf.c mempool.c fdcxt.c kafka.c
OBJS    := $(SRCS:.c=.o)
TARGET  := server

# Base flags
CFLAGS  := -Wall -g -O2
LDFLAGS := -lm

# Pull in pkg-config flags for glib + rdkafka
CFLAGS  += $(shell pkg-config --cflags glib-2.0 rdkafka)
LDLIBS  += $(shell pkg-config --libs glib-2.0 rdkafka)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
