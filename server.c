#include "headers.h"

#define SERVER_PORT 5193
#define BACKLOG 10

int main(int argc, char const *argv[]) {

    int listensd, connsd;
    struct sockaddr_in saddr, caddr;

    if(argc<2){
        fprintf(stderr, "[Usage] %s msg\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    char *msg = argv[1];

printf("[Server] Hello\n");

    /* Socket - Listen*/
    if( (listensd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ){
        perror("[Server] Error in socket\n");
        exit(EXIT_FAILURE);
    }
printf("[Server] Listensd is %d\n", listensd);

    /* Bind */
    memset((void *)&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(SERVER_PORT);
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if( (bind(listensd, (struct sockaddr *)&saddr, sizeof(saddr))) < 0 ){
        perror("[Server] Error in bind\n");
        exit(EXIT_FAILURE);
    }

    /* Listen */
    if( (listen(listensd, BACKLOG)) < 0 ){
        perror("[Server] Error in listen");
        exit(EXIT_FAILURE);
    }
printf("[Server] Ready to accept on port %d\n", SERVER_PORT);

    while(1){
        /* Accept */
        if( (connsd = accept(listensd, (struct sockaddr *)NULL, NULL)) < 0 ){
            perror("[Server] Error in listen");
            exit(EXIT_FAILURE);
        }

        if( (write(connsd, msg, strlen(msg))) != strlen(msg) ){
            perror("[Server] Error in write");
            exit(EXIT_FAILURE);
        }

        if( (close(connsd)) < 0){
            perror("[Server] Error in close");
            exit(EXIT_FAILURE);
        }
printf("[Server] Bye\n");
    }

    exit(EXIT_SUCCESS);
}
