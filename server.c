#include "common.c"
#include "config.h"

int nextseqnum;
char *msg;
struct sockaddr_in servaddr, cliaddr;
socklen_t len;
    int sockd; // global until setop calls for setsock
    char *status = "okclient";
    char *spath = DEFAULT_PATH; // root folder for server

void sendack(int sockd, int op, int cliseq, int pktleft, char *status){
    struct pkt ack;

    nextseqnum++;
    ack = makepkt(op, nextseqnum, cliseq, pktleft, status);

    check(sendto(sockd, &ack, HEADERSIZE+ack.size, 0, (struct sockaddr *)&cliaddr, sizeof(struct sockaddr_in)), "sendack:sendto");
printf("[Server] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
}

int waitforack(){
    struct pkt ack;

printf("[Server] Waiting for ack...\n");
    check(recvfrom(sockd, &ack, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len), "waitforack:recvfrom");
printf("[Server] Received ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, ack.data);

    if(ack.op == ACK_POS){
        return 1;
    }
    return 0;
}

void makelist(char** res, const char* path){
    char command[DATASIZE];
    FILE* file;

    sprintf(command, "ls %s | cat > list.txt", path);
    system(command);

    file = fopen("list.txt", "r");
    fread(*res, DATASIZE, 1, file);
}

void managelist(){
    char *res = malloc(((DATASIZE)-1) * sizeof(char)); // client has to put \0 at the end
    char **resptr = &res;
    struct pkt listpkt;

    makelist(resptr, spath);
    listpkt = makepkt(CARGO, nextseqnum, 1, 1, res);
printf("[Server] Sending list [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", listpkt.op, listpkt.seq, listpkt.ack, listpkt.pktleft, listpkt.size, (char *)listpkt.data);
    check(sendto(sockd, &listpkt, DATASIZE, 0, (struct sockaddr *)&cliaddr, sizeof(struct sockaddr_in)) , "main:sendto");
}

void get(char *pathname){
    int n; // for waitforack
    char *res = malloc(((DATASIZE)-1) * sizeof(char)); // client has to put \0 at the end
    struct pkt getpkt;
    char *localpathname = malloc(DATASIZE * sizeof(char));
printf("Server folder:%s %s\n", SERVER_FOLDER, pathname);
    sprintf(localpathname, "%s%s", SERVER_FOLDER, pathname);
printf("localpathname: %s\n", localpathname);
    int filength = calculate_filelength(localpathname);
    int fd = open(localpathname, O_RDONLY, 0666);

    read(fd, res, filength);
    getpkt = makepkt(CARGO, nextseqnum, 1, 1, res);

    do {
printf("[Server] Sending cargo [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", getpkt.op, getpkt.seq, getpkt.ack, getpkt.pktleft, getpkt.size, (char *)getpkt.data);
        check(sendto(sockd, &getpkt, DATASIZE, 0, (struct sockaddr *)&cliaddr, sizeof(struct sockaddr_in)) , "main:sendto");

        n = waitforack();

    } while(n==0);
}


int setop(struct elab opdata){
    struct pkt synack;
    // TMP for testing 1 packet scenarios
    int listsize = 1;

    sendack(sockd, ACK_POS, opdata.clipacket.seq, listsize, status);

printf("[Server] Waiting for synack...\n"); // TODO in SERVER_TIMEOUT
    recvfrom(sockd, &synack, DATASIZE, 0, (struct sockaddr *)&cliaddr, &len);
printf("[Server] Received [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", synack.op, synack.seq, synack.ack, synack.pktleft, synack.size, synack.data);

    if(opdata.clipacket.op == SYNOP_LIST && synack.op == ACK_POS){
        managelist();
        return 1;
    }
    if(opdata.clipacket.op == SYNOP_GET && synack.op == ACK_POS){
printf("File requested %s\n", opdata.clipacket.data);
        get(opdata.clipacket.data);
        return 1;
    }

printf("[Server] Client operation aborted\n");
        return 0;
}

int main(int argc, char const* argv[]) {
    struct pkt cpacket;
    struct elab epacket;

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
    len = sizeof(struct sockaddr_in);

    while(1){
        /* Infinite receiving  */
printf("[Server] Waiting for synop...\n");
        check_mem(memset((void *)&cliaddr, 0, sizeof(struct sockaddr_in)), "main:memset:cliaddr");
        check_mem(memset((void *)&cpacket, 0, sizeof(struct pkt)), "main:memset:cpacket");
        check(recvfrom(sockd, &cpacket, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len), "main:rcvfrom");
printf("[Server] Received [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", cpacket.op, cpacket.seq, cpacket.ack, cpacket.pktleft, cpacket.size, cpacket.data);

        check_mem(memset((void *)&epacket, 0, sizeof(struct elab)), "main:memset:epacket");
        memcpy(&epacket.cliaddr, &cliaddr, len);
        epacket.clipacket = makepkt(cpacket.op, cpacket.seq, cpacket.ack, cpacket.pktleft, cpacket.data);
printf("[Server] Creating elab [addr:?][port:%d][op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", epacket.cliaddr.sin_port, epacket.clipacket.op, epacket.clipacket.seq, epacket.clipacket.ack, epacket.clipacket.pktleft, epacket.clipacket.size, epacket.clipacket.data);

        /* Operation selection */
        switch (cpacket.op) {

            case SYNOP_LIST: // list
                if(setop(epacket)){
printf("[Server] Operation cmd:%d seq:%d status:completed successfully\n\n", epacket.clipacket.op, epacket.clipacket.seq);
                } else {
printf("[Server] Operation cmd:%d seq:%d status:completed unsuccessfully\n\n", epacket.clipacket.op, epacket.clipacket.seq);
                }
                break;

            case SYNOP_GET: // get
                if(setop(epacket)){
                    get(epacket.clipacket.data);
printf("[Server] Operation cmd:%d seq:%d status:completed successfully\n\n", epacket.clipacket.op, epacket.clipacket.seq);
                } else {
printf("[Server] Operation cmd:%d seq:%d status:completed unsuccessfully\n\n", epacket.clipacket.op, epacket.clipacket.seq);
                }
                break;

            case SYNOP_PUT: // put
                // TMP disabled by default
                sendack(sockd, ACK_NEG, cpacket.seq, 0, "generic negative status");
printf("My job here is done\n\n");
                break;

            default:
printf("[Server] Can't handle this packet\n\n");
                sendack(sockd, ACK_NEG, cpacket.seq, 0, "malformed packet");
                break;
        }
    }

    exit(EXIT_FAILURE);
}
