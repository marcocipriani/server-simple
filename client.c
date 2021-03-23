#include "headers.h"
#include "config.h"
#include "common.c"

int me;
int sockd;
int seqnum;
struct sockaddr_in servaddr, cliaddr;
socklen_t len;

void setsock(){
    check( (sockd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) ), "Error creating the datagram socket");
/* TODO client id = port
    memset((void *)&cliaddr, 0, sizeof(cliaddr));
    socklen_t clen = sizeof(cliaddr);
    check( (getsockname(sockd, (struct sockaddr *)&cliaddr, &clen) ), "Error getting sock name");
    me = ntohs(cliaddr.sin_port);
*/

    memset((void *)&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    if( (inet_pton(AF_INET, SERVER_ADDR, &servaddr.sin_addr)) <= 0){
        printf("Error inet_pton\n");
        exit(EXIT_FAILURE);
    }

printf("[Client #%d] Ready to contact %s at %d.\n", me, SERVER_ADDR, SERVER_PORT);
}

void setop(int cmd){
    int n;
    char *rcvbuf;
    struct pkt *cpacket;

    seqnum++;
    cpacket = makepkt(seqnum, 1/*ack*/, 1/*flag*/, cmd, "./");

printf("[Client #%d] Sending [seq:%d][ack:%d][flag:%d][op:%d][length:%d][data:%s]\n", me, cpacket->seq, cpacket->ack, cpacket->flag, cpacket->op, cpacket->length, cpacket->data);
    check(sendto(sockd, (struct pkt *)cpacket, cpacket->length + HEADERSIZE, 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) , "Error setop");

    rcvbuf = malloc(BUFSIZE * sizeof(char));
    n = recvfrom(sockd, rcvbuf, BUFSIZE, 0, (struct sockaddr *)&servaddr, &len);
printf("[Server] Operation %s\n", rcvbuf);
}

void list(){
    int n;
    char buffer[BUFSIZE + 1]; // 1024 + \0

    n = recvfrom(sockd, buffer, BUFSIZE, 0, (struct sockaddr *)&servaddr, &len);
    if(n > 0){
        printf("Available files on server:\n");
            buffer[n] = '\0';
            fprintf(stdout, "%s", buffer);
    } else {
        printf("No available files on server\n");
    }
}

int main(int argc, char const *argv[]) {
    int op;

    me = getpid();
    seqnum = 0;

    /* Usage */
    if(argc > 2){
        fprintf(stderr, "Quickstart with %s, extra parameters are discarded.\n[Usage] %s [<operation-number>]\n", argv[1], argv[0]);
    }

printf("Welcome to server-simple app, client #%d\n", me);

    /* Socket + filling servaddr */
    setsock();

    if(argc == 2){
        op = atoi(argv[1]);
        goto quickstart;
    }

    while (1) {
        /* Parsing input */
        printf("\nAvailable operations: 1 (list available files), 2 (get a file), 3 (put a file), 0 (exit).\nChoose an operation and press ENTER: ");
        fscanf(stdin, "%d", &op);

quickstart:
        /* Operation selection */
        switch (op) {
            case 1: // list
printf("[Client #%d] Requesting list operation...\n", me);
                setop(1);
                list();
                break;
            case 2: // get
printf("[Client #%d] Requesting get operation...\n", me);
                setop(2);
                break;
            case 3: // put
printf("[Client #%d] Requesting put operation...\n", me);
                setop(3);
                break;
            case 0: // exit
                fprintf(stdout, "Bye client #%d\n", me);
                exit(EXIT_SUCCESS);
            default:
                printf("No operation associated with %d\n", op);
                break;
        }
    }

    exit(EXIT_FAILURE);
}
