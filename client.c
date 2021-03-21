#include "headers.h"
#include "error.c"

#define SERVER_PORT 5193
#define SERVER_ADDR "127.0.0.1"
#define BUFSIZE 1024

int me;
int sockd;
struct sockaddr_in servaddr;

void setsock(){
    check( (sockd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) ), "Error creating the datagram socket");

    memset((void *)&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    // servaddr.sin_addr.s_addr = INADDR_ANY;
    if( (inet_pton(AF_INET, SERVER_ADDR, &servaddr.sin_addr)) <= 0){
        printf("Error inet_pton\n");
        exit(EXIT_FAILURE);
    }

printf("[Client #%d] Ready to contact %s at %d.\n", me, SERVER_ADDR, SERVER_PORT);
}

socklen_t len;

void setop(int cmd){
    int n;
    char *rcvbuf;
    char *oper;


    switch (cmd) {
        case 0:
            oper = "list";
    }

    check( (sendto(sockd, (char *)oper, sizeof(oper), 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) ), "Error setop" );

    rcvbuf = malloc(BUFSIZE * sizeof(char));
    n = recvfrom(sockd, rcvbuf, BUFSIZE, 0, (struct sockaddr *)&servaddr, &len);
printf("server said: %s\n", rcvbuf);
}

void list(){
    int n;
    char buffer[BUFSIZE + 1]; // 1024 + \0

    n = recvfrom(sockd, buffer, BUFSIZE, 0, (struct sockaddr *)&servaddr, &len);
    if(n > 0){
        printf("Available files on server:\n");
            buffer[n] = '\0';
            fprintf(stdout, "%s", buffer);
    }
}

int main(int argc, char const *argv[]) {
    int oper;

    /* Usage */
    if(argc > 2){
        fprintf(stderr, "Extra parameters are discarded. [Usage] %s <command number>\n", argv[0]);
    }

    me = getpid();

printf("Welcome to server-simple app, client #%d\n", me);

    setsock();

    while (1) {
        printf("\nAvailable operations: 0 (list available files), 1 (get a file), 2 (put a file), 3 (exit).\nChoose an operation and press ENTER: ");
        fscanf(stdin, "%d", &oper);
printf("[Client #%d] Requesting %d operation...\n", me, oper);

        /* Operation selection */
        switch (oper) {
            case 0: // list
                setop(0);
                list();
                break;
            case 1: // get
                printf("get\n");
                break;
            case 2: // put
                printf("put\n");
                break;
            case 3: // exit
                fprintf(stdout, "Bye client #%d\n", me);
                exit(EXIT_SUCCESS);
            default:
                printf("No operation associated with %d\n", oper);
                break;
        }
    }

    exit(EXIT_FAILURE);
}

void closeconn(){
    if(sockd){
        check(close(sockd), "[Client] Error in closing the client socket");
    }
}

void tcpconn(){
    check( (sockd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) ), "Error creating the stream socket");

    memset((void *)&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_ADDR, &servaddr.sin_addr);
    check(connect(sockd, (struct sockaddr *)&servaddr, sizeof(servaddr)), "Error in connecting to the server");
printf("[Client #%d] Connected to %s at %d.\n", me, SERVER_ADDR, SERVER_PORT);
}
