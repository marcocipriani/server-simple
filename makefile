CC=gcc
CFLAGS=-Wall -Wextra -O2 -pthread #-g
SRCS=client.c server.c
OBJS=$(SRCS:.c=.o)
BIN=$(SRCS:.c=.dSYM)
TRGTS=$(SRCS:.c=)

.PHONY: all clean

all:$(OBJS)

%o:%c
	$(CC) $(CFLAGS) $< -o $(@:.o=)

clean:
	-$(RM) $(OBJS) $(TRGTS)
