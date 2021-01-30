#include "headers.h"

#define SERVER_PORT   5193

int main(int argc, char const *argv[]) {

    int sockfd;
    struct sockaddr_in saddr;

    int n;
    char buffer[1025]; // 1024 + \0

printf("[Client] Hello\n");

    /* Socket */
    if( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ){
        perror("[Client] Error creating the socket\n");
        exit(EXIT_FAILURE);
    }
printf("[Client] Sockfd is %d\n", sockfd);

    /* Connect */
    memset((void *)&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(SERVER_PORT);
    // TODO saddr.sin_addr = htonl("127.0.0.1");
    inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
    if( (connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr))) < 0 ){
        perror("[Client] Error in connecting to the server\n");
        exit(EXIT_FAILURE);
    }
printf("[Client] Connected to 127.0.0.1 at %d\n", SERVER_PORT);

    /* Job */
    while( (n = read(sockfd, buffer, 1024)) > 0 ){
        if(n<0){
            perror("[Server] Error in read\n");
            exit(EXIT_FAILURE);
        }

        buffer[n] = 0; // TODO or '\0'
        fputs(buffer, stdout);
    }


    /* Close */
    if( (close(sockfd)) < 0 ){
        perror("[Client] Error in closing the client socket\n");
        exit(EXIT_FAILURE);
    }
printf("[Client] Bye\n");

    exit(EXIT_SUCCESS);
}
