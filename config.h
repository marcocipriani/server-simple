// Server settings
#define SERVER_PORT 5193
#define SERVER_ADDR "127.0.0.1"
#define BACKLOG 10
#define DEFAULT_PATH "~"
#define SERVER_BUFSIZE 1024

// Client settings
#define CLIENT_PORT 4193
#define CLIENT_BUFSIZE 1024

// Packet settings
#define MAXTRANSUNIT 1500
#define HEADERSIZE 5*sizeof(int)
#define DATASIZE MAXTRANSUNIT-HEADERSIZE
