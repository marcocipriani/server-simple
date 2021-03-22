#include "headers.h"
#include "error.c"

#define SERVER_PORT 5193
#define BACKLOG 10
#define BUFSIZE 1024

int sockd;
char *msg;
struct sockaddr_in cliaddr;
socklen_t len;

void setsock(){
    struct sockaddr_in servaddr;

    check((sockd = socket(AF_INET, SOCK_DGRAM, 0)), "[Server] Error in socket");

    memset((void *)&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    check((bind(sockd, (struct sockaddr *)&servaddr, sizeof(servaddr))),"[Server] Error in bind");

fprintf(stdout, "[Server] Ready to accept on port %d\n", SERVER_PORT);
}

int waitforop(){
    char *cmdreq;
    cmdreq = malloc(BUFSIZE * sizeof(char));

    memset((void *)&cliaddr, 0, sizeof(cliaddr));
    len = sizeof(cliaddr);
    recvfrom(sockd, cmdreq, BUFSIZE, 0, (struct sockaddr *)&cliaddr, &len);

    char *ok = "ok";
    check( sendto(sockd, (char *)ok, strlen(ok), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr)) , "Server reply error");
printf("[Server] Operation %s from client #%d %s\n", cmdreq, cliaddr.sin_port, ok);

    if(strcmp(cmdreq, "list") == 0){
        return 1;
    } else if (strcmp(cmdreq, "get") == 0){
        return 2;
    } else if (strcmp(cmdreq, "put") == 0){
        return 3;
    }

    return -1;
}

void list(char** res, const char* path){
    char command[BUFSIZE];
    FILE* file;

    sprintf(command, "ls %s | cat > list.txt", path);
    system(command);

    file = fopen("list.txt", "r");
    fread(*res, BUFSIZE, 1, file);
}

int main(int argc, char const* argv[]) {
    int op;

    const char *path;
    char *res = malloc(BUFSIZE * sizeof(char));
    char **resptr = &res;

    if(argc<2){
        fprintf(stderr, "[Usage]: %s <path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    path = argv[1];

    setsock();

    while(1){

        op = waitforop();

        switch (op) {
            case 1: // list
                list(resptr, path);
                check( sendto(sockd, (char *)res, strlen(res), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr)) , "Server sending res error");
fprintf(stdout, "[Server] Sending list to client #%d...\n\n", cliaddr.sin_port);
                break;
            case 2: // get
                break;
            case 3: // put
                break;
            default:
fprintf(stdout, "[Server] Can't handle client #%d operation...\n\n", cliaddr.sin_port);
                break;
        }
    }

    exit(EXIT_FAILURE);
}
