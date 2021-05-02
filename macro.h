// Server settings
#define SERVER_PORT 5193
#define SERVER_ADDR "127.0.0.1"
#define BACKLOG 10
#define SERVER_RCVBUFSIZE 30
#define SERVER_TIMEOUT 60
#define DEFAULT_PATH "~"
#define SERVER_FOLDER "./server-files/"

// Client settings
#define CLIENT_PORT 4193
#define CLIENT_RCVBUFSIZE 30
#define CLIENT_TIMEOUT 30
#define CLIENT_FOLDER "./client-files/"

// Packet settings
#define MAXTRANSUNIT 1500
#define HEADERSIZE 5*sizeof(int)
#define DATASIZE MAXTRANSUNIT-HEADERSIZE
#define WSIZE 10
#define TIMEINTERVAL 500000
#define MAXSEQNUM 65536
// OP codes
#define SYNOP_ABORT 0
#define SYNOP_LIST 1
#define SYNOP_GET 2
#define SYNOP_PUT 3
#define ACK_POS 4
#define ACK_NEG 5
#define CARGO 6

#define SIGRETRANSMIT SIGUSR1
#define SIGLASTACK SIGUSR2
