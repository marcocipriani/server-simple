#include "headers.h"
#include "config.h"
#include "error.c"

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