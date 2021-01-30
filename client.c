#include "headers.h"

#define SERVER_PORT   5193

int main(int argc, char const *argv[]) {

    int sockfd;
    struct sockaddr_in saddr;

    int me;

    int n;
    char buffer[1025]; // 1024 + \0

    me = getpid();


    /* Socket */
    if( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ){
        fprintf(stdout, "[Client #%d] Error creating the socket\n", me);
        exit(EXIT_FAILURE);
    }

    /* Connect */
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

    /* Job */
    while( (n = read(sockfd, buffer, 1024)) > 0 ){
        if(n<0){
            fprintf(stdout, "[Client #%d] Error in read\n", me);
            exit(EXIT_FAILURE);
        }

        buffer[n] = '\0';
        fprintf(stdout, "%s\n", buffer);
    }


    /* Close */
    if( (close(sockfd)) < 0 ){
        fprintf(stdout, "[Client #%d] Error in closing the client socket\n", me);
        exit(EXIT_FAILURE);
    }
fprintf(stdout, "[Client #%d] Bye\n", me);

    exit(EXIT_SUCCESS);
}
