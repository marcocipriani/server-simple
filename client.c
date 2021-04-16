#include "common.c"
#include "config.h"

int me;
int sockd;
int nextseqnum;
struct sockaddr_in servaddr, cliaddr;
socklen_t len;

int setop(int cmd, int pktleft, void *arg){
    struct pkt synop, ack, synack;
    char *status = malloc((DATASIZE)*sizeof(char));

    nextseqnum++;
    synop = makepkt(cmd, nextseqnum, 0, pktleft, arg);

printf("[Client #%d] Sending synop [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, synop.op, synop.seq, synop.ack, synop.pktleft, synop.size, (char *)synop.data);
    check(sendto(sockd, &synop, synop.size + HEADERSIZE, 0, (struct sockaddr *)&servaddr, sizeof(struct sockaddr_in)) , "setop:sendto");

printf("[Client #%d] Waiting patiently for ack in max %d seconds...\n", me, CLIENT_TIMEOUT);
    check(recvfrom(sockd, &ack, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, &len), "setop:recvfrom");
printf("[Client #%d] Received ack from server [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);

    if(ack.op == ACK_POS){
printf("[Server] Operation %d #%d permitted [estimated packets: %d]\nContinue? [Y/n] ", synop.op, synop.seq, ack.pktleft);
        fflush(stdin);
        if(getchar()=='n'){
            status = "noserver";
            cmd = ACK_NEG;
        } else {
            cmd = ACK_POS;
            status = "okserver";
        }

        synack = makepkt(cmd, nextseqnum, 0, pktleft, status);
        check(sendto(sockd, &synack, synack.size + HEADERSIZE, 0, (struct sockaddr *)&servaddr, sizeof(struct sockaddr_in)) , "setop:sendto");
printf("[Client #%d] Sending synack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, synack.op, synack.seq, synack.ack, synack.pktleft, synack.size, (char *)synack.data);

        if(cmd == ACK_NEG){
printf("[Client] Aborting operation\n");
            return 0;
        }
        return 1;
    }

printf("[Server] Operation %d #%d not permitted\n", synop.op, synop.seq);

    return 0;
}

void sendack(int sockd, int op, int cliseq, int pktleft, char *status){
    struct pkt ack;

    nextseqnum++;
    ack = makepkt(op, nextseqnum, cliseq, pktleft, status);

    check(sendto(sockd, &ack, HEADERSIZE+ack.size, 0, (struct sockaddr *)&servaddr, sizeof(struct sockaddr_in)), "sendack:sendto");
printf("[Server] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
}

void list(){
    int n;
    struct pkt listpkt;
    int fd = open("./client-files/client-list.txt", O_CREAT|O_RDWR|O_TRUNC, 0666);

    n = recvfrom(sockd, &listpkt, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, &len);
printf("[Client #%d] Received list from server [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:...]\n", me, listpkt.op, listpkt.seq, listpkt.ack, listpkt.pktleft, listpkt.size);
    if(n > 0){
        printf("Available files on server:\n");
            //buffer[n] = '\0';
            fprintf(stdout, "%s", listpkt.data);
            write(fd, listpkt.data, listpkt.size);
    } else {
        printf("No available files on server\n");
        write(fd, "No available files on server\n", 30);
    }
}

void get(char *filename){
    int n;
    struct pkt getpkt;
    char *localpathname = malloc(DATASIZE * sizeof(char));
    sprintf(localpathname, "%s%s", CLIENT_FOLDER, filename);
    int fd = open(localpathname, O_CREAT|O_RDWR|O_TRUNC, 0666);

    n = recvfrom(sockd, &getpkt, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, &len);
printf("[Client #%d] Received cargo from server [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, getpkt.op, getpkt.seq, getpkt.ack, getpkt.pktleft, getpkt.size, (char *)getpkt.data);
    if(n > 0){
            write(fd, getpkt.data, getpkt.size);
            sendack(sockd, ACK_POS, getpkt.seq, 0, "okclient");
    } else {
        printf("Nothing from server\n");
            sendack(sockd, ACK_NEG, getpkt.seq, 0, "okserver");
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
//seq = 1+rand()%99;
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
            case SYNOP_LIST: // list
                // ask for which path to list
                if(setop(SYNOP_LIST, 0, arg)){
printf("[Client #%d] Looking for list of default folder...\n", me);
                    list();
                }
                break;

            case SYNOP_GET: // get
                printf("Type filename to get and press ENTER: ");
                fscanf(stdin, "%s", arg);
                if(setop(SYNOP_GET, 0, arg)){
printf("[Client #%d] Waiting for %s...\n", me, arg);
                    get(arg);
                }
                break;

            case SYNOP_PUT: // put
put:
                printf("Type filename to put and press ENTER: ");
                fscanf(stdin, "%s", arg);
                if(calculate_numpkts(arg) < 0){
                    printf("File not found\n");
                    goto put;
                }
                if(setop(SYNOP_PUT, filesize, arg)){
printf("[Client #%d] Sending %s in the space...\n", me, arg);
                }
                break;

            case SYNOP_ABORT: // exit
                fprintf(stdout, "Bye client #%d\n", me);
                exit(EXIT_SUCCESS);
            default:
                printf("No operation associated with %d\n", cmd);
                break;
        }
    }

    exit(EXIT_FAILURE);
}
