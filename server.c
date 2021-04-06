#include "common.c"
#include "config.h"

int nextseqnum;
char *msg;
struct sockaddr_in servaddr, cliaddr;
socklen_t len;

void sendack(int sockd, int cliseq, int pktleft, char *status){
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
    int sockd;

    /* Usage */
    if(argc > 2){
        fprintf(stderr, "Path from argv[1] set, extra parameters are discarded. [Usage]: %s [<path>]\n", argv[0]);
    }

    /* Init */
    if(argc > 1) spath = (char *)argv[1];
printf("[Server] Root folder: %s\n", spath);
    nextseqnum = 0;
    memset((void *)&servaddr, 0, sizeof(struct sockaddr_in));
    sockd = setsock(&servaddr, NULL, SERVER_PORT, 0, 1);
    cpacket = (struct pkt *)check_mem(malloc(sizeof(struct pkt)), "main:malloc:cpacket");
    len = sizeof(cliaddr);

    // TMP for testing list
    char *res = malloc((DATASIZE-1) * sizeof(char)); // client has to put \0 at the end
    char **resptr = &res;
    struct pkt *listpkt = malloc(sizeof(struct pkt));
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
                sendack(sockd, cpacket->seq, listsize, status);

                // TMP for testing list
                list(resptr, spath);
                listpkt = makepkt(1, 1, 1, 1, res);
                check(sendto(sockd, listpkt, DATASIZE, 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr)) , "main:sendto");

printf("[Server] Sending list to client #%d...\n\n", cliaddr.sin_port);
printf("List: %s\n", listpkt->data);
                break;
            case 2: // get

                sendack(sockd, cpacket->seq, filesize, status);
printf("My job here is done\n\n");
                break;
            case 3: // put
                sendack(sockd, cpacket->seq, 0, status);
printf("My job here is done\n\n");
                break;
            default:
printf("[Server] Can't handle this packet\n\n");
                // SEND A NACK? to protect from wrong packets
                sendack(sockd, cpacket->seq, 0, "malformed packet");
                break;
        }
    }

    exit(EXIT_FAILURE);
}
