#include "config.h"
#include "common.c"

int me;
int sockid; //per ora sockd = sockid
int nextseqnum;
struct sockaddr_in servaddr, cliaddr;
socklen_t len;
char rcvbuf[45000];
//pthread_mutex_t wsizemutex;
//int wsize = 10; per ora disabilitato

/* OPERAZIONE GESTIONE SEMAFORO PER LA WSIZE per ora disabilitato
int pthread_mutex_init(pthread_mutex_t *,const pthread_mutexattr_t *);  //dichiarazioni per evitare i warning
int pthread_mutex_lock(pthread_mutex_t *);
int pthread_mutex_unlock(pthread_mutex_t *);
int mainthput(int,int,int); */



void setsock2(){ //futuri cambiamenti +numport
  	struct timeval tout;

  	sockid = check(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), "setsock:socket");
/* TODO client id = port
    memset((void *), 0, sizeof(cliaddr));
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
  	check(setsockopt(sockid,SOL_SOCKET,SO_RCVTIMEO,&tout,sizeof(tout)), "setsock:setsockopt");

printf("[Client #%d] Ready to contact %s at %d.\n", me, SERVER_ADDR, SERVER_PORT);
}

struct pkt setop(int cmd,int ownseq, int pktleft, void *arg){
  struct pkt synop, ack;
  //nextseqnum++;

  synop=makepkt(cmd, ownseq, 0, pktleft, arg, strlen((char *)arg));

printf("[Client #%d] Sending synop [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, synop.op, synop.seq, synop.ack, synop.pktleft, synop.size, (char *)synop.data);
  check(sendto(sockid, &synop, synop.size + HEADERSIZE, 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) , "setop:sendto");
printf("[Client #%d] Waiting patiently for ack in max %d seconds...\n", me, CLIENT_TIMEOUT);
  check(recvfrom(sockid, &ack, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, &len), "setop:recvfrom");
printf("[Client #%d] Received ack from server [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);

  if(strcmp(ack.data, "ok")==0){
printf("[Client #%d] Connection established \n", me);
  return ack;
  } // else other statuses
  exit(EXIT_FAILURE);
}


void sendack2(int sockid, struct pkt ack){ //ownseq for pkt seq of process, ack = ownseq of other process

    //struct sockaddr *servaddr;
    check(sendto(sockid,&ack, HEADERSIZE+ack.size, 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) , "sendack:sendto");
    printf("[Client] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);

}
int setop2(int cmd, int pktleft, void *arg){
    struct pkt synop, ack, synack;
    char *status = malloc((DATASIZE)*sizeof(char));
    
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

        synack = makepkt(cmd, nextseqnum, 0, pktleft, status, strlen(status));
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



int freespacebuf(int totpkt){
	size_t totpktsize;
	int res;

	totpktsize =(size_t) (totpkt*sizeof(char))*(DATASIZE*sizeof(char));
	res = sizeof(rcvbuf)-totpktsize;
	if (res >=0) {
	return 1;
  } else{
        return 0;
  }
}


void sendack(int sockd, int op, int cliseq, int pktleft, char *status){
    struct pkt ack;

    nextseqnum++;
    ack = makepkt(op, nextseqnum, cliseq, pktleft, status,strlen(status));

    check(sendto(sockd, &ack, HEADERSIZE+ack.size, 0, (struct sockaddr *)&servaddr, sizeof(struct sockaddr_in)), ":sendto");
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


int get(int iseq, void *pathname){

  int fd;
  size_t filesize;
  int npkt,edgepkt;
  int pos,lastpktsize,initseqserver;
  //char *tmpbuff[];
  struct pkt rack, sack, cargo;

  setsock2();
  rack = setop(2, iseq, 0, pathname); /* ricevo da setop #pkt da ricevere */

  npkt = rack.pktleft;
  edgepkt=npkt; /*porcata per tenere a mente #pkt totali del file da ricevere SOL: do + while!!!*/

  initseqserver = rack.seq;
  iseq++; //un pacchetto lo ha già inviato nella semop con numero di seq = iseq
  //controllo su buffer CLIENT
  if(freespacebuf(npkt)){
      sack = makepkt( 4, iseq, initseqserver, 0, "ok", 2);
      sendack2(sockid, sack);
      //memset(sack.data,0,sack.size);
/*----ricezione cargo---*/
receiver:
      while(npkt>0)  {
        check(recvfrom(sockid,&cargo, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, &len), "GET-client:recvfrom Cargo"); /*attesa di ricevere cargo dal server */
        //scriviiiii
        printf("pacchetto ricevuto: seq %d, ack %d, pktleft %d, size %d, data %s \n", cargo.seq, cargo.ack, cargo.pktleft, cargo.size, cargo.data);
        pos=(cargo.seq - initseqserver);
        printf("cargo->seq: %d \n",cargo.seq);
        printf("initseqserver: %d \n",initseqserver);
        printf("pos: %d \n",pos);

        if(pos>edgepkt && pos<0){
          printf("numero sequenza pacchetto ricevuto fuori range \n");
          return 0;
          /* DA MODIFICARE IN SEGUITO*/
        }
        else if((rcvbuf[pos*(DATASIZE)])==0){	/*sono nell'intervallo corretto*/
          if(cargo.seq==(initseqserver+edgepkt-1)){
            lastpktsize = cargo.size;
          }
          memcpy(&rcvbuf[pos*(DATASIZE)],cargo.data,DATASIZE);
          sack=makepkt( 4, iseq, cargo.seq, 0, "ok",2);
          sendack2(sockid, sack);
          printf("il pacchetto #%d e' stato scritto in pos:%d del buffer\n",cargo.seq,pos);
        }
        else{
          sack=makepkt( 4, iseq, cargo.seq, 0, "ok",2);
          sendack2(sockid, sack);
          //memset(sack.data,0,sack.size);
          goto receiver;    /*il pacchetto viene scartato*/
        }
      npkt--;
      //  memset(sack.data,0,sack.size);
      //  free(cargo);
      }
      filesize = (size_t)((DATASIZE)*(edgepkt-1))+lastpktsize; //dimensione effettiva del file
      printf("(edgepkt-1): %d \n",(edgepkt-1));
      printf("lastpktsize: %d \n",lastpktsize);
      printf("filesize: %ld \n",filesize);
      fd = open("tumadre",O_RDWR|O_TRUNC|O_CREAT,0666);
      writen(fd,rcvbuf,filesize);
      printf("il file %s e' stato correttamente scaricato\n",(char *)pathname); //UTOPIA
      return 1;
    }
  else{
    printf("non ho trovato spazio libero nel buff \n");
    sack=makepkt( 4, iseq, initseqserver, 0, "Full_Client_Buff",16);
    sendack2(sockid, sack); //ack negativo
    return 0;
  }
}


int main(int argc, char const *argv[]) {

  int cmd;
  char *arg;
	int totpkt;
	int seq;
	struct stat filestat;


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
    seq = 1+rand()%99;
    nextseqnum = 0;
    if(argc == 2){
        cmd = atoi(argv[1]);
        goto quickstart;
    }

    printf("Welcome to server-simple app, client #%d\n", me);


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
                if(setop2(SYNOP_LIST, 0, arg)){
printf("[Client #%d] Looking for list of default folder...\n", me);
                    list();
                }
                break;

            case SYNOP_GET: // get
                printf("Type filename to get and press ENTER: ");
                fscanf(stdin, "%s", arg);
                printf("[Client #%d] File selected is %s", me, arg);
                if(get(seq, arg)){
                  //if(setop2(SYNOP_GET, 0, arg)){
                  printf("GET conclusa\n");
                }
                else{
                  printf("GET: qualcosa e' andato storto\n");
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
                if(setop2(SYNOP_PUT, filesize, arg)){
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
