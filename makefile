CC = gcc
CFLAGS = -Wall -O2 -pthread
LDFLAGS = -pthread

SRCS = main.c webserver.c http_conn.c threadpool.c timer.c log.c
OBJS = $(SRCS:.c=.o)
TARGET = tiny_server

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean