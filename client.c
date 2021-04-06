#include "common.c"
#include "config.h"

int me;
int sockd;
int nextseqnum;
struct sockaddr_in servaddr, cliaddr;
socklen_t len;

int setop(int cmd, int pktleft, void *arg){
    int ret; // for returning values
    char *rcvbuf;
    struct pkt *synop, *ack;

    nextseqnum++;
    synop = (struct pkt *)check_mem(makepkt(cmd, nextseqnum, 0, pktleft, arg), "setop:makepkt");

printf("[Client #%d] Sending synop [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, synop->op, synop->seq, synop->ack, synop->pktleft, synop->size, (char *)synop->data);
    check(sendto(sockd, (struct pkt *)synop, synop->size + HEADERSIZE, 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) , "setop:sendto");

printf("[Client #%d] Waiting patiently for ack in max %d seconds...\n", me, CLIENT_TIMEOUT);
    ack = (struct pkt *)check_mem(malloc(sizeof(struct pkt *)), "setop:malloc");
    check(recvfrom(sockd, ack, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, &len), "setop:recvfrom");
printf("[Client #%d] Received ack from server [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, ack->op, ack->seq, ack->ack, ack->pktleft, ack->size, (char *)ack->data);

    if(strcmp(ack->data, "ok")==0){
        return 1;
    } // else other statuses
    return 0;
}

void list(){
    int n;
    char buffer[DATASIZE]; // 1024 + \0
    struct pkt *listpkt = malloc(sizeof(struct pkt));
    int fd = open("./client-files/client-list.txt", O_CREAT|O_RDWR|O_TRUNC, 0666);

    n = recvfrom(sockd, listpkt, DATASIZE, 0, (struct sockaddr *)&servaddr, &len);
    if(n > 0){
        printf("Available files on server:\n");
            //buffer[n] = '\0';
            fprintf(stdout, "%s", listpkt->data);
            write(fd, listpkt->data, listpkt->size);
    } else {
        printf("No available files on server\n");
        write(fd, "No available files on server\n", 30);
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
    arg = (char *)check_mem(malloc(DATASIZE*sizeof(char)), "main:malloc");
    me = getpid();
    //servaddr = malloc(sizeof(struct sockaddr_in));
    memset((void *)&servaddr, 0, sizeof(struct sockaddr_in));
    sockd = setsock(&servaddr, SERVER_ADDR, SERVER_PORT, CLIENT_TIMEOUT, 0);
    nextseqnum = 0;
    if(argc == 2){
        cmd = atoi(argv[1]);
        goto quickstart;
    }
    printf("Welcome to server-simple app, client #%d\n", me);

    // TMP for testing put
    int filesize = 0;

    while (1) {
        /* Infinite parsing input */
        printf("\nAvailable operations: 1 (list available files), 2 (get a file), 3 (put a file), 0 (exit).\nChoose an operation and press ENTER: ");
        if( !fscanf(stdin, "%d", &cmd)){
            fflush(stdin);
            continue;
        }

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
                check(calculate_numpkts(arg), "file not found");
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
