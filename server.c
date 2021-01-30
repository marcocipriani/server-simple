#include "headers.h"

#define SERVER_PORT 5193
#define BACKLOG 10

int main(int argc, char const *argv[]) {

    int listensd, connsd;
    struct sockaddr_in saddr, caddr;
    socklen_t len;

    char *msg;

    if(argc<2){
        fprintf(stderr, "[Usage]: %s <msg>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    msg = argv[1];

    /* Socket - Listen*/
    if( (listensd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ){
        fprintf(stderr, "[Server] Error in socket\n");
        exit(EXIT_FAILURE);
    }

    /* Bind */
    memset((void *)&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(SERVER_PORT);
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if( (bind(listensd, (struct sockaddr *)&saddr, sizeof(saddr))) < 0 ){
        fprintf(stderr, "[Server] Error in bind\n");
        exit(EXIT_FAILURE);
    }

    /* Listen */
    if( (listen(listensd, BACKLOG)) < 0 ){
        fprintf(stderr, "[Server] Error in listen");
        exit(EXIT_FAILURE);
    }
fprintf(stdout, "[Server] Ready to accept on port %d\n", SERVER_PORT);

    while(1){
        /* Accept */
        len = sizeof(caddr);
        if( (connsd = accept(listensd, (struct sockaddr *)&caddr, &len)) < 0 ){
            fprintf(stderr, "[Server] Error in listen");
            exit(EXIT_FAILURE);
        }

        // TODO printf("%u\n", caddr.sin_addr.s_addr);

        if( (write(connsd, msg, strlen(msg))) != strlen(msg) ){
            fprintf(stderr, "[Server] Error in write");
            exit(EXIT_FAILURE);
        }

        if( (close(connsd)) < 0){
            fprintf(stderr, "[Server] Error in close");
            exit(EXIT_FAILURE);
        }

fprintf(stdout, "[Server] Bye client\n");
    }

    exit(EXIT_SUCCESS);
}
