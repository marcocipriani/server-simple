#include "headers.h"
#include "error.c"

#define SERVER_PORT 5193
#define BUFSIZE 1024

int main(int argc, char const *argv[]) {

    int sockfd;
    struct sockaddr_in saddr;

    int me;

    int n;
    char buffer[BUFSIZE + 1]; // 1024 + \0

    me = getpid();

    /* Socket */
    check( (sockfd = socket(AF_INET, SOCK_STREAM, 0) ), "Error creating the socket");

    /* Connect */
    memset((void *)&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
    check(connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)), "Error in connecting to the server");
printf("[Client #%d] Connected to 127.0.0.1 at %d.\n", me, SERVER_PORT);

    /* Job */
    while( (n = read(sockfd, buffer, BUFSIZE)) > 0 ){
        check(n, "[Client] Error in read");

printf("Available files on server:\n");
        buffer[n] = '\0';
        fprintf(stdout, "%s\n", buffer);
    }


    /* Close */
    check(close(sockfd), "[Client] Error in closing the client socket");

fprintf(stdout, "[Client #%d] Bye\n", me);

    exit(EXIT_SUCCESS);
}
