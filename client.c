#include "headers.h"

#define SERVER_PORT   5193

int me;

int sockfd;
char buffer[1025]; // 1024 + \0

void sockinit(){
    if( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ){
        fprintf(stdout, "[Client #%d] Error creating the socket\n", me);
        exit(EXIT_FAILURE);
    }
}

void sockclose(){
    if( (close(sockfd)) < 0 ){
        fprintf(stdout, "[Client #%d] Error in closing the client socket\n", me);
        exit(EXIT_FAILURE);
    }
fprintf(stdout, "[Client #%d] Bye\n", me);
}

void servconnect(){
    struct sockaddr_in saddr;

    memset((void *)&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(SERVER_PORT);
    // TODO saddr.sin_addr = htonl("127.0.0.1");
    inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
    if( (connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr))) < 0 ){
        fprintf(stdout, "[Client #%d] Error in connecting to the server\n", me);
        exit(EXIT_FAILURE);
    }
printf("[Client #%d] Connected to 127.0.0.1 at %d.\n", me, SERVER_PORT);
}

int list(){
    int n;

    while( (n = read(sockfd, buffer, 1024)) > 0 ){
        if(n<0){
            fprintf(stdout, "[Client #%d] Error in read\n", me);
            exit(EXIT_FAILURE);
        }

        buffer[n] = '\0';
        fprintf(stdout, "%s\n", buffer);
    }

    exit(EXIT_SUCCESS);
}

int main(int argc, char const *argv[]) {
    int opt;

    /* Usage */
    if(argc<2){
        fprintf(stderr, "[Usage] %s <command number> [<filename>]\ncommand number: 0 (list), 1 (get), 2 (put)\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    me = getpid();
    opt = atoi(argv[1]);
printf("[Client #%d] Requesting %d command.\n", me, opt);

    sockinit();
    servconnect();

    /* Job selection */
    switch (opt) {
        case 0:
            list();
            break;
        case 1:
            printf("get\n");
            break;
        case 2:
            printf("put\n");
            break;
    }

    sockclose();

    exit(EXIT_SUCCESS);
}
