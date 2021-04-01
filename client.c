#include "common.c"
#include "config.h"

int me;
int sockd;
int nextseqnum;
struct sockaddr_in servaddr, cliaddr;
socklen_t len;

FILE *file;
char *filename = "/home/fabio/Scrivania/progetto/server-simple/listricevuto.txt" ;
int fd;


void setsock(){
    struct timeval tout;

    sockd = check(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), "setsock:socket");
/* TODO client id = port
    memset((void *)&cliaddr, 0, sizeof(cliaddr));
    socklen_t clen = sizeof(cliaddr);
    check( (getsockname(sockd, (struct sockaddr *)&cliaddr, &clen) ), "Error getting sock name");
    me = ntohs(cliaddr.sin_port);
*/
    check_mem(memset((void *)&servaddr, 0, sizeof(servaddr)), "setsock:memset");
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    check(inet_pton(AF_INET, SERVER_ADDR, &servaddr.sin_addr), "setsock:inet_pton");

	tout.tv_sec = CLIENT_TIMEOUT;
	tout.tv_usec = 0;
    check(setsockopt(sockd,SOL_SOCKET,SO_RCVTIMEO,&tout,sizeof(tout)), "setsock:setsockopt");

printf("[Client #%d] Ready to contact %s at %d.\n", me, SERVER_ADDR, SERVER_PORT);
}

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
        return ack->pktleft; // così abbiamo il residuo dei pacchetti rimanenti
    } // else other statuses
    return -1;
}

void list(int totalpackets ){

    int n;
    char * arg;
    char * buffAvailable;
    char buffer[DATASIZE]; // 1024 + \0
    char buffer2[4000];
    int fd;

    struct pkt *ack2;
    struct pkt *pkt;

    //synop-list

    buffAvailable = malloc(totalpackets*sizeof(struct pkt*) );

    //allocare spazio per il packet = (...)
    pkt = (struct pkt*) malloc(sizeof(struct pkt*));

    //recvfrom totalpackets cargo in questo caso(banale) ne ricevo solo uno
    //ma se faccio un iterazione posso ricevere più pacchetti

    recvfrom(sockd,pkt,MAXTRANSUNIT,0,NULL, NULL);
    printf(" stampa pkt %s",pkt->data);

    //abbiamo aperto o creato un file per salvare la lista
    fd = open(filename,O_CREAT| O_RDWR|O_TRUNC,0666);
    if (fd == -1){printf("errore nella open \n");}

    //abbiamo aperto lo stream a quel file
    file = fdopen(fd,"w+");
    if(file == NULL){printf("errore nella fdopen \n");}

    fprintf(file,"%s",pkt->data);

    //allocare lo spazio per l' ack = (...)
    ack2 = (struct pkt*) malloc(sizeof(struct pkt*));

    ack2 = check_mem(makepkt(4, nextseqnum,pkt->seq, 0, NULL),"errore makepkt ack");  //vedi common
    printf("\n%d\n",ack2->op);
    sendto(sockd,ack2,MAXTRANSUNIT,0,(struct sockaddr *)&servaddr,sizeof(servaddr) );

}

int main(int argc, char const *argv[]) {
    int cmd;
    char *arg;
    int totalpkt;
    /* Usage */
    if(argc > 3){
        fprintf(stderr, "Quickstart with %s, extra parameters are discarded.\n[Usage] %s [<operation-number>]\n", argv[1], argv[0]);
    }

    /* Init */
    if(argc == 2){
        cmd = atoi(argv[1]);
        goto quickstart;
    }

    arg = (char *)check_mem(malloc(DATASIZE*sizeof(char)), "main:malloc");
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

                totalpkt = setop(1, 0, arg);//perchè setop ha come ritorno un intero
                printf("total pkt = %d \n", totalpkt);
                list(totalpkt);
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
