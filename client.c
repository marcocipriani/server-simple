#include "common.c"
#include "config.h"

int me;
int sockd;
int nextseqnum;
struct sockaddr_in servaddr, cliaddr;
socklen_t len;

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
        return 1;
    } // else other statuses
    return 0;
}

// void thread_job_s(int fd,char *cmd,char *fname,char *dt,struct sockaddr_in *servaddr,socklen_t len)     funzione J


void list(int cmd){
    int n;
    int nextseqnum;
    char buffer[DATASIZE]; // 1024 + \0
    char dirname[MAXLINE] = {"/home/user/Scrivania/"};  /*Nome della cartella locale*/
    int fd;

    struct pkt *synop-list;
    struct pkt *ack;
    struct pkt *pkt;

    nextseqnum++;
    // creazione di paccketto di synop_list
    synop_list = (struct pkt *)check_mem(makepkt(cmd, nextseqnum, 0, NULL, arg), "setop-list:makepkt");
    printf("[Client #%d] Sending synop_list [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, synop_list->op, synop_list->seq, synop_list->ack, synop_list->pktleft, synop_list->size, (char *)synop_list->data);
    check(sendto(sockd,synop_list, synop_list->size + HEADERSIZE, 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) , "sendto");

    // attesa ack
    printf("[Client #%d] Waiting patiently for ack in max %d seconds...\n", me, CLIENT_TIMEOUT);
    ack = (struct pkt *)check_mem(malloc(sizeof(struct pkt *)), "setop:malloc");
    check(recvfrom(sockd, ack, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, sizeof(servaddr)), "recvfrom");
    printf("[Client #%d] Received ack from server [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, ack->op, ack->seq, ack->ack, ack->pktleft, ack->size, (char *)ack->data);

    // attesa pkt
    printf("[Client #%d] Waiting patiently for pkt in max %d seconds...\n", me, CLIENT_TIMEOUT);
    pkt = (struct pkt *)check_mem(malloc(sizeof(struct pkt *)), "setop:malloc");  // malloc struct pkt
    check(recvfrom(sockd, pkt, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, sizeof(servaddr)), "recvfrom");
    printf("[Client #%d] Received pkt from server [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, ack->op, ack->seq, ack->ack, ack->pktleft, ack->size, (char *)ack->data);


        if(strcmp(pkt->data, "ok")==0){
          // invio ack
          printf("[Client #%d] Sending ack in max %d seconds...\n", me, CLIENT_TIMEOUT);
          ack = (struct pkt *)check_mem(malloc(sizeof(struct pkt *)), "setop:malloc");
          check(sendto(sockd, ack, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, sizeof(servaddr)), "sendto");
          //ricezione pkt
          while(pkt->pktleft > 0){
            check(rcvfrom(sockd, pkt, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr,  sizeof(servaddr)), "rcvfrom");
            // non si deve riunire il file?
          }
          //write listbuffer on filelist

          /* Crea il file per contenere la lista dei nomi dei file */
            strncat(dirname,string,strlen(string));  /*Per avere il percorso completo del file da aprire*/

            fd = open(dirname,O_CREAT | O_RDWR | O_TRUNC,0644);
            if(fd == -1)
              exit_on_error("error client open filelist");

            //thread_job_r(sockfd,fd,"list",string,dirname);  // funzione Jer


        }else{ // else other statuses
          // send ack to finish
          printf("[Client #%d] Sending ack in max %d seconds...\n", me, CLIENT_TIMEOUT);
          ack = (struct pkt *)check_mem(malloc(sizeof(struct pkt *)), "setop:malloc");
          check(sendto(sockd, ack, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, sizeof(servaddr)), "sendto");
          return 0;
        }



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
                if( setop(1, 0, arg) ){
printf("[Client #%d] Looking for list of default folder...\n", me);
                }
                list(cmd);
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
