#include "headers.h"
#include "error.c"

#define SERVER_PORT 5193
#define BACKLOG 10


int main(int argc, char const *argv[]) {

    int listensd, connsd;
    struct sockaddr_in saddr, caddr;
    socklen_t len;

    const char *path; // TODO const or not?
    char *buffer, *command;

    int fd;

    if(argc<2){
        fprintf(stderr, "[Usage]: %s <path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    path = argv[1];

    /* Socket */
    check((listensd = socket(AF_INET, SOCK_STREAM, 0)), "[Server] Error in socket");

    /* Bind */
    memset((void *)&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(SERVER_PORT);
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    check((bind(listensd, (struct sockaddr *)&saddr, sizeof(saddr))),"[Server] Error in bind");

    /* Listen */
    check((listen(listensd, BACKLOG)), "[Server] Error in listen");
fprintf(stdout, "[Server] Ready to accept on port %d\n", SERVER_PORT);

    while(1){
        /* Accept */
        len = sizeof(caddr);
        check((connsd = accept(listensd, (struct sockaddr *)&caddr, &len)), "[Server] Error in listen");

        // TODO printf("%u\n", caddr.sin_addr.s_addr);

        // create list
        sprintf(command, "ls %s | cat > list.txt", path);
        system(command);
        // save list on buffer
        fd = open("list.txt", O_RDONLY);
        read(fd, buffer, 1024);
        // write socket
        write(connsd, buffer, strlen(buffer));

        check((close(connsd)), "[Server] Error in close");

fprintf(stdout, "[Server] Bye client\n");
    }

    exit(EXIT_SUCCESS);
}
