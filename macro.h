// Server settings
#define SERVER_PORT 5193
#define SERVER_ADDR "127.0.0.1"
#define BACKLOG 10
#define DEFAULT_PATH "~"
#define SERVER_FOLDER "./server-files/"
#define SERVER_RCVBUFSIZE 30
#define SERVER_TIMEOUT 3
#define SERVER_NUMTHREADS 5
#define SERVER_SWND_SIZE 10
#define PACKET_LOSS_SERVER 50

#define TIMEINTERVAL 50000

// Client settings
#define CLIENT_PORT 4193
#define CLIENT_FOLDER "./client-files/"
#define CLIENT_LIST_FILE "./client-files/list.txt"
#define CLIENT_RCVBUFSIZE 40
#define CLIENT_TIMEOUT 3
#define CLIENT_NUMTHREADS 1
#define CLIENT_SWND_SIZE 10
#define PACKET_LOSS_CLIENT 20

// Sender signals
#define SIGFINAL SIGUSR1
//#define SIGRETRANSMIT SIGUSR2

// Packet settings
#define MAXPKTSIZE 1500
#define HEADERSIZE 5*sizeof(int)
#define DATASIZE MAXPKTSIZE-HEADERSIZE
#define MAXSEQNUM 65536

// OP codes
#define SYNOP_ABORT 0
#define SYNOP_LIST 1
#define SYNOP_GET 2
#define SYNOP_PUT 3
#define ACK_POS 4
#define ACK_NEG 5
#define CARGO 6

// ACK and validity status
#define CARGO_OK "received correctly"
#define CARGO_MISSING "missing base packet"
#define FILE_AVAILABLE "File available"
#define FILE_NOT_AVAILABLE "File not available"
