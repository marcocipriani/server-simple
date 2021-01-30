PREFIX=../../
CC=gcc
CC_OPTS=-Wall -g --std=gnu99 -I$(PREFIX)/include -I$(PREFIX)/src/client/ 


LIBS=-lpthread -lreadline -lwebsockets


OBJS = 	packet_handler.o\
	deferred_packet_handler.o\
	orazio_client.o\
	serial_linux.o\
	orazio_print_packet.o\
	ventry.o\
	orazio_shell_globals.o\
	orazio_shell_commands.o


HEADERS = packet_header.h\
	  packet_operations.h\
	  packet_handler.h\
	  deferred_packet_handler.h\
	  orazio_packets.h\
	  orazio_print_packet.h\
	  ventry.h

INCLUDES=$(addprefix $(PREFIX)/include/, $(HEADERS))

BINS= 	packet_handler_test.bin \
	deferred_action_test.bin\
	packet_query_response_test.bin\
	orazio_client_test.bin\
	orazio_shell.bin\
	orazio_websocket_server.bin

.phony:	clean all

all:	$(BINS)

#common objects
%.o:	$(PREFIX)/src/common/%.c $(INCLUDES)
	$(CC) $(CC_OPTS) -c  $<

#host test
%.o:	$(PREFIX)/src/host_test/%.c $(INCLUDES)
	$(CC) $(CC_OPTS) -c  $<

#client 
%.o:	$(PREFIX)/src/client/%.c $(INCLUDES)
	$(CC) $(CC_OPTS) -c  $<

%.o:	$(PREFIX)/src/client/%.cpp $(INCLUDES)
	$(CXX) $(CXX_OPTS) -c  $<

%.bin:  %.o $(OBJS) $(INCLUDES)
	$(CC) $(CC_OPTS) -o $@ $< $(OBJS) $(LIBS)

clean:
	rm -rf $(OBJS) $(BINS) *~

