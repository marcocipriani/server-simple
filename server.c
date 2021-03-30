#include "common.c"
#include "config.h"

int sockd;
int nextseqnum;
char *msg;
struct sockaddr_in cliaddr;
socklen_t len;

void setsock(){
    struct sockaddr_in servaddr;

    check(sockd = socket(AF_INET, SOCK_DGRAM, 0), "setsock:socket");

    check_mem(memset((void *)&servaddr, 0, sizeof(servaddr)), "setsock:memset");
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    check(bind(sockd, (struct sockaddr *)&servaddr, sizeof(servaddr)) , "setsock:bind");

fprintf(stdout, "[Server] Ready to accept on port %d\n\n", SERVER_PORT);
}


void sendack(int cliseq, int pktleft, char *status){
    struct pkt *ack;

    nextseqnum++;
    ack = (struct pkt *)check_mem(makepkt(4, nextseqnum, cliseq, pktleft, status), "sendack:makepkt");

    check(sendto(sockd, ack, HEADERSIZE+strlen(status), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr)) , "sendack:sendto");
printf("[Server] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", ack->op, ack->seq, ack->ack, ack->pktleft, ack->size, (char *)ack->data);
}

void list(char** res, const char* path){
    char command[DATASIZE];
    FILE* file;

    sprintf(command, "ls %s | cat > list.txt", path);
    system(command);

    file = fopen("list.txt", "r");
    fread(*res, DATASIZE, 1, file);
}

int main(int argc, char const* argv[]) {
    char *spath = DEFAULT_PATH; // root folder for server
    struct pkt *cpacket;

    /* Usage */
    if(argc > 2){
        fprintf(stderr, "Path from argv[1] set, extra parameters are discarded. [Usage]: %s [<path>]\n", argv[0]);
    }

    /* Init */
    if(argc > 1) spath = (char *)argv[1];
printf("[Server] Root folder: %s\n", spath);
    nextseqnum = 0;
    setsock();
    cpacket = (struct pkt *)check_mem(malloc(sizeof(struct pkt)), "main:malloc:cpacket");
    len = sizeof(cliaddr);

    // TMP for testing list
    char *res = malloc((DATASIZE-1) * sizeof(char)); // client has to put \0 at the end
    char **resptr = &res;
    // TMP for testing ack status
    char *status = "ok";
    int filesize = 0, listsize = 0;

    while(1){
        /* Infinite receiving  */
        check_mem(memset((void *)&cliaddr, 0, sizeof(cliaddr)), "main:memset");
        check(recvfrom(sockd, cpacket, HEADERSIZE+DATASIZE, 0, (struct sockaddr *)&cliaddr, &len), "main:rcvfrom");
printf("[Server] Received [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", cpacket->op, cpacket->seq, cpacket->ack, cpacket->pktleft, cpacket->size, cpacket->data);

        /* Operation selection */
        switch (cpacket->op) {
            case 1: // list
                sendack(cpacket->seq, listsize, status);

                // TMP for testing list
                list(resptr, spath);
                check(sendto(sockd, (char *)res, strlen(res), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr)) , "main:sendto");

printf("[Server] Sending list to client #%d...\n\n", cliaddr.sin_port);
                break;
            case 2: // get
                 // calculate the size of the arg file
                sendack(cpacket->seq, filesize, status);
printf("My job here is done\n\n");
                break;
            case 3: // put
                sendack(cpacket->seq, 0, status);
printf("My job here is done\n\n");
                break;
            default:
printf("[Server] Can't handle this packet\n\n");
                // SEND A NACK? to protect from wrong packets
                sendack(cpacket->seq, 0, "malformed packet");
                break;
        }
    }

    exit(EXIT_FAILURE);
}
