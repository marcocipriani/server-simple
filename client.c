#include "common.c"
#include "config.h"

int me;
int sockd;
int nextseqnum;
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

int setop(int cmd, int pktleft, void *arg){
    int ret; // for returning values
    char *rcvbuf;
    struct pkt *synop, *ack;

    nextseqnum++;
    synop = makepkt(cmd, nextseqnum, 0, pktleft, arg);

printf("[Client #%d] Sending synop [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, synop->op, synop->seq, synop->ack, synop->pktleft, synop->size, (char *)synop->data);
    check(sendto(sockd, (struct pkt *)synop, synop->size + HEADERSIZE, 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) , "Error sending synop");

printf("[Client #%d] Waiting patiently for ack...\n", me);
    ack = (struct pkt *)malloc(sizeof(struct pkt *));
    ret = recvfrom(sockd, ack, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, &len);
    check(ret, "Nothing received from server");
printf("[Client #%d] Received ack from server [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, ack->op, ack->seq, ack->ack, ack->pktleft, ack->size, (char *)ack->data);

    if(strcmp(ack->data, "ok")==0){
        return 1;
    } // else other statuses
    return 0;
}

void list(){
    int n;
    char buffer[DATASIZE]; // 1024 + \0

    n = recvfrom(sockd, buffer, DATASIZE, 0, (struct sockaddr *)&servaddr, &len);
    if(n > 0){
        printf("Available files on server:\n");
            buffer[n] = '\0';
            fprintf(stdout, "%s", buffer);
    } else {
        printf("No available files on server\n");
    }
}

int main(int argc, char const *argv[]) {
    int cmd;
    char *arg;

    /* Usage */
    if(argc > 3){
        fprintf(stderr, "Quickstart with %s, extra parameters are discarded.\n[Usage] %s [<operation-number>]\n", argv[1], argv[0]);
    }

    /* Init */
    if(argc == 2){
        cmd = atoi(argv[1]);
        goto quickstart;
    }
    arg = malloc(DATASIZE*sizeof(char));
    me = getpid();
printf("Welcome to server-simple app, client #%d\n", me);
    setsock();
    nextseqnum = 0;

    // TMP for testing put
    int filesize = 0;

    while (1) {
        /* Infinite parsing input */
        printf("\nAvailable operations: 1 (list available files), 2 (get a file), 3 (put a file), 0 (exit).\nChoose an operation and press ENTER: ");
        fscanf(stdin, "%d", &cmd);

quickstart:
        /* Operation selection */
        switch (cmd) {
            case 1: // list
                // ask for which path to list
                if( setop(1, 0, arg) ){
printf("[Client #%d] Looking for list of default folder...\n", me);
                }
                list();
                break;
            case 2: // get
                printf("Type filename to get and press ENTER: ");
                fscanf(stdin, "%s", arg);
                if(setop(2, 0, arg)){
printf("[Client #%d] Waiting for %s...\n", me, arg);
                }
                break;
            case 3: // put
                printf("Type filename to put and press ENTER: ");
                fscanf(stdin, "%s", arg);
                // calculate the size of the arg file
                if(setop(3, filesize, arg)){
printf("[Client #%d] Sending %s in the space...\n", me, arg);
                }
                break;
            case 0: // exit
                fprintf(stdout, "Bye client #%d\n", me);
                exit(EXIT_SUCCESS);
            default:
                printf("No operation associated with %d\n", cmd);
                break;
        }
    }

    exit(EXIT_FAILURE);
}
