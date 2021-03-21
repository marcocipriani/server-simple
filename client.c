#include "headers.h"
#include "error.c"

#define SERVER_PORT 5193
#define BUFSIZE 1024
#define SADDR "127.0.0.1"

int me;
int sockfd;

void closeconn(){
    if(!sockfd){
        check(close(sockfd), "[Client] Error in closing the client socket");
    }
}

void openconn(){
    struct sockaddr_in saddr;

    check( (sockfd = socket(AF_INET, SOCK_STREAM, 0) ), "Error creating the stream socket");

    memset((void *)&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SADDR, &saddr.sin_addr);
    check(connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)), "Error in connecting to the server");
printf("[Client #%d] Connected to %s at %d.\n", me, SADDR, SERVER_PORT);
}

void list(){
    int n;
    char buffer[BUFSIZE + 1]; // 1024 + \0

    while( (n = read(sockfd, buffer, BUFSIZE)) > 0 ){
        check(n, "[Client] Error in read");

printf("Available files on server:\n");
        buffer[n] = '\0';
        fprintf(stdout, "%s\n", buffer);
    }
}

int main(int argc, char const *argv[]) {
    int opt;

    /* Usage */
    if(argc != 1){
        fprintf(stderr, "Extra parameters are discarded. [Usage] %s\n", argv[0]);
    }

    me = getpid();
    sockfd = 0;

printf("Welcome to server-simple app, client #%d\n", me);

    while (1) {
        printf("Available command numbers: 0 (list available files), 1 (get a file), 2 (put a file), 3 (exit)\nPlease insert a command number and press ENTER: ");
        fscanf(stdin, "%d", &opt);

printf("[Client #%d] Requesting %d command.\n", me, opt);

        /* Job selection */
        switch (opt) {
            case 0: // list
            //write(sockfd, 1, sizeof(int));
                openconn();
                list();
                closeconn();
                break;
            case 1: // get
                printf("get\n");
                break;
            case 2: // put
                printf("put\n");
                break;
            case 3: // exit
                closeconn();
                fprintf(stdout, "Bye client #%d\n", me);
                exit(EXIT_SUCCESS);
            default:
                printf("Invalid command number\n");
                break;
        }
    }

    exit(EXIT_FAILURE);
}
