#include "config.h"
#include "common.c"

int me;
int sockd;
struct sockaddr_in servaddr, cliaddr;
socklen_t len;
int nextseqnum;
char rcvbuf[45000];

// setsock1() in common.c
void setsock2(){
    struct timeval tout;

    sockd = check(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), "setsock:socket");

    check_mem(memset((void *)&servaddr, 0, sizeof(servaddr)), "setsock:memset");
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    check(inet_pton(AF_INET, SERVER_ADDR, &servaddr.sin_addr), "setsock:inet_pton");

    tout.tv_sec = CLIENT_TIMEOUT;
    tout.tv_usec = 0;
    check(setsockopt(sockd,SOL_SOCKET,SO_RCVTIMEO,&tout,sizeof(tout)), "setsock:setsockopt");

printf("[Client #%d] Ready to contact %s at %d.\n", me, SERVER_ADDR, SERVER_PORT);
}

/*
 *  function: setop
 *  ----------------------------
 *  Check operation validity with server
 *
 *  cmd: SYNOP_ABORT, SYNOP_LIST, SYNOP_GET, SYNOP_PUT
 *  pktleft: (only for put) how many packets the file is made of
 *  arg: (list) not-used (get) name of the file to get (put) name of the file to put
 *
 *  return: quantity of packets the operation should use
 *  error: 0
 */
int setop(int cmd, int pktleft, void *arg){ // int ownseq required?
    struct pkt synop, ack, synack;
    char *status = malloc((DATASIZE)*sizeof(char));
    me = getpid(); // different me for threads doing setop

    nextseqnum++;
    synop = makepkt(cmd, nextseqnum, 0, pktleft, strlen((char *)arg), arg);

printf("[Client #%d] Sending synop [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, synop.op, synop.seq, synop.ack, synop.pktleft, synop.size, (char *)synop.data);
    check(sendto(sockd, &synop, (HEADERSIZE) + synop.size, 0, (struct sockaddr *)&servaddr, sizeof(struct sockaddr_in)) , "setop:sendto");

printf("[Client #%d] Waiting patiently for ack in max %d seconds...\n", me, CLIENT_TIMEOUT);
    check(recvfrom(sockd, &ack, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, &len), "setop:recvfrom");
printf("[Client #%d] Received ack from server [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);

    if(ack.op == ACK_POS){
printf("Operation %d #%d permitted [estimated packets: %d]\nContinue? [Y/n] ", synop.op, synop.seq, ack.pktleft);
        fflush(stdin);
        if(getchar()=='n'){
            status = "noserver";
            cmd = ACK_NEG;
        }else{
            cmd = ACK_POS;
            status = "okserver";
        }

        nextseqnum++;
        synack = makepkt(cmd, nextseqnum, 0, pktleft, strlen(status), status);
printf("[Client #%d] Sending synack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, synack.op, synack.seq, synack.ack, synack.pktleft, synack.size, (char *)synack.data);
        check(sendto(sockd, &synack, synack.size + HEADERSIZE, 0, (struct sockaddr *)&servaddr, sizeof(struct sockaddr_in)) , "setop:sendto");

        if(cmd == ACK_NEG){
printf("Aborting operation...\n");
            return 0;
        }
        return ack.pktleft;
    }

printf("Operation %d #%d not permitted\n", synop.op, synop.seq);
    return 0;
}

/*
 *  function: sendack
 *  ----------------------------
 *  Send a ack packet
 *
 *  sockd: socket descriptor used for sending
 *  op: flag for postive or negative status ack
 *  cliseq: sequence number of the packet to acknowledge
 *  pktleft: // TODO ?
 *  status: verbose description of the ack
 *
 *  return: -
 *  error: -
 */
void sendack(int sockd, int op, int cliseq, int pktleft, char *status){
    struct pkt ack;

    nextseqnum++;
    ack = makepkt(op, nextseqnum, cliseq, pktleft, strlen(status), status);

    check(sendto(sockd, &ack, HEADERSIZE+ack.size, 0, (struct sockaddr *)&servaddr, sizeof(struct sockaddr_in)), ":sendto");
printf("[Client #%d] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
}

void sendack2(int sockd, struct pkt ack){ //ownseq for pkt seq of process, ack = ownseq of other process

    check(sendto(sockd,&ack, HEADERSIZE+ack.size, 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) , "sendack:sendto");
printf("[Client #%d] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
}

int freespacebuf(int totpkt){
	size_t totpktsize;
	int res;

	totpktsize =(size_t) (totpkt*sizeof(char))*(DATASIZE*sizeof(char));
	res = sizeof(rcvbuf)-totpktsize;
	if (res >=0){
	       return 1;
    }else{
        return 0;
    }
}

/*
 *  function: list
 *  ----------------------------
 *  Receive and print list sent by the server
 *
 *  return: -
 *  error: -
 */
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
    }else{
        printf("No available files on server\n");
        write(fd, "No available files on server\n", 30);
    }
}

int get(int iseq, void *pathname, int pktleft){

    int fd;
    size_t filesize;
    int npkt,edgepkt;
    int pos,lastpktsize,initseqserver;
    //char *tmpbuff[];
    struct pkt rack, sack, cargo;

    //setsock2();
    //rack = setop(2, iseq, 0, pathname); /* ricevo da setop #pkt da ricevere */

    npkt = pktleft;
    edgepkt=npkt; /*porcata per tenere a mente #pkt totali del file da ricevere SOL: do + while!!!*/

    initseqserver = rack.seq;
    iseq++; //un pacchetto lo ha già inviato nella semop con numero di seq = iseq
    //controllo su buffer CLIENT
    if(freespacebuf(npkt)){
        sack = makepkt(ACK_POS, iseq, initseqserver, 0, 2, "ok");
        sendack2(sockd, sack);

receiver:
        while(npkt>0){
            check(recvfrom(sockd,&cargo, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, &len), "GET-client:recvfrom Cargo");
printf("pacchetto ricevuto: seq %d, ack %d, pktleft %d, size %d, data %s \n", cargo.seq, cargo.ack, cargo.pktleft, cargo.size, cargo.data);
            pos=(cargo.seq - initseqserver);
printf("cargo->seq: %d \n",cargo.seq);
printf("initseqserver: %d \n",initseqserver);
printf("pos: %d \n",pos);

            if(pos>edgepkt && pos<0){
printf("numero sequenza pacchetto ricevuto fuori range \n");
                return 0;
            } else if((rcvbuf[pos*(DATASIZE)])==0){ // sono nell'intervallo corretto
                if(cargo.seq==(initseqserver+edgepkt-1)){
                    lastpktsize = cargo.size;
                }
                memcpy(&rcvbuf[pos*(DATASIZE)],cargo.data,DATASIZE);
                sack=makepkt(ACK_POS, iseq, cargo.seq, 0, 2, "ok");
                sendack2(sockd, sack);
printf("il pacchetto #%d e' stato scritto in pos:%d del buffer\n",cargo.seq,pos);
            }else{
                sack=makepkt(ACK_POS, iseq, cargo.seq, 0, 2, "ok");
                sendack2(sockd, sack);
                goto receiver; // il pacchetto viene scartato
            }
            npkt--;
        }

        filesize = (size_t)((DATASIZE)*(edgepkt-1))+lastpktsize; //dimensione effettiva del file
printf("(edgepkt-1): %d \n",(edgepkt-1));
printf("lastpktsize: %d \n",lastpktsize);
printf("filesize: %ld \n",filesize);
        fd = open("tumadre",O_RDWR|O_TRUNC|O_CREAT,0666);
        writen(fd,rcvbuf,filesize);
printf("il file %s e' stato correttamente scaricato\n",(char *)pathname); //UTOPIA
        return 1;
    }else{
        printf("non ho trovato spazio libero nel buff \n");
        sack=makepkt(4, iseq, initseqserver, 0, 16, "Full_Client_Buff");
        sendack2(sockd, sack); //ack negativo
        return 0;
    }
}

// TODEL
void get2(char *filename){
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
    }else{
        printf("Nothing from server\n");
            sendack(sockd, ACK_NEG, getpkt.seq, 0, "okserver");
    }
}

int main(int argc, char const *argv[]) {
    int cmd;
    char *arg;
    int totpkt; // ret for setop, pktleft of received ack from server
    char *localpathname = malloc(DATASIZE * sizeof(char)); // put: client folder + pathname
    int filesize; // put: filesize of file to put

    /* Usage */
    if(argc > 3){
        fprintf(stderr, "Quickstart with %s, extra parameters are discarded.\n[Usage] %s [<operation-number>]\n", argv[1], argv[0]);
    }

    /* Init */
    me = getpid();
printf("Welcome to server-simple app, client #%d\n", me);
    arg = check_mem(malloc(DATASIZE*sizeof(char)), "main:malloc:arg");
    memset((void *)&servaddr, 0, sizeof(struct sockaddr_in));
    sockd = setsock(&servaddr, SERVER_ADDR, SERVER_PORT, CLIENT_TIMEOUT, 0);
    nextseqnum = 0; // nextseqnum = 1+rand()%99;
    if(argc == 2){
        cmd = atoi(argv[1]);
        goto quickstart;
    }

    while (1) {
        /* Infinite parsing input */
        printf("\nAvailable operations: 1 (list available files), 2 (get a file), 3 (put a file), 0 (exit).\nChoose an operation and press ENTER: ");

        if(!fscanf(stdin, "%d", &cmd)){
            printf("Invalid operation code\n");
            fflush(stdin);
            continue;
        }

quickstart:
        /* Operation selection */
        switch(cmd){
            case SYNOP_LIST: // list
                // TODO ask for which path to list instead of SERVER_FOLDER
                arg = SERVER_FOLDER;
                if((totpkt = setop(SYNOP_LIST, 0, arg)) > 0){
                    list();
                }
                break;

            case SYNOP_GET: // get
                printf("Type filename to get and press ENTER: ");
                fscanf(stdin, "%s", arg);
                if((totpkt = setop(SYNOP_GET, 0, arg)) > 0){
                    get(nextseqnum, arg, totpkt);
                }
                break;

            case SYNOP_PUT: // put
fselect:
                printf("Type filename to put and press ENTER: ");
                fscanf(stdin, "%s", arg);
                sprintf(localpathname, "%s%s", CLIENT_FOLDER, arg);
                if((filesize = calculate_numpkts(localpathname)) < 1){
                    printf("File not found\n");
                    fflush(stdin);
                    goto fselect;
                }
                if((totpkt = setop(SYNOP_PUT, filesize, arg)) > 0){
                    // put()
                }
                break;

            case SYNOP_ABORT: // exit
                printf("Bye client #%d\n", me);
                exit(EXIT_SUCCESS);

            default:
                printf("No operation associated with %d\n", cmd);
                break;
        } // end switch
    } // end while

    exit(EXIT_FAILURE);
}
