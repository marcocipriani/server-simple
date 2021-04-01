#include "common.c"
#include "config.h"

int sockd;
int nextseqnum;
char *msg;
char rcvbuf[45000]; //buffer per la put
struct sockaddr_in cliaddr;
socklen_t len;

void setsock(){
    struct sockaddr_in servaddr;

    check(sockd = socket(AF_INET, SOCK_DGRAM, 0), "setsock:socket");

    check_mem(memset((void *)&servaddr, 0, sizeof(servaddr)), "setsock:memset");
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    check(bind(sockd, (struct sockaddr *)&servaddr, sizeof(servaddr)) , "setsock:bind");

fprintf(stdout, "[Server] Ready to accept on port %d\n\n", SERVER_PORT);
}

void sendack(int idsock, int cliseq, int pktleft, char *status){ //aggiunto idsock per specificare quale socket invia l'ack
    struct pkt *ack;

    nextseqnum++;
    ack = (struct pkt *)check_mem(makepkt(4, nextseqnum, cliseq, pktleft, status), "sendack:makepkt");

    check(sendto(idsock, ack, HEADERSIZE+strlen(status), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr)) , "sendack:sendto");
printf("[Server] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", ack->op, ack->seq, ack->ack, ack->pktleft, ack->size, (char *)ack->data);
}

int freespacebuf(int totpkt){
	size_t totpktsize;
	int res;

	totpktsize =(size_t) (totpkt*sizeof(char))*(DATASIZE*sizeof(char));
	res = sizeof(rcvbuf)-totpktsize;
	if (res >=0) {
	return 1;
	} else return 0;
}

void setrcvputsock(){ //crea la socket rcvputsockd specifica per la put
	struct timeval tout;

    rcvputsockd = check(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), "setsockoutd:socket");
    tout.tv_sec = SERVER_TIMEOUT;
	tout.tv_usec = 0;
    check(setsockopt(rcvputsockd,SOL_SOCKET,SO_RCVTIMEO,&tout,sizeof(tout)), "setrcvputsock:setsockopt");


fprintf(stdout, "[Server] Ready to accept connection from client for put operation \n");
}

void *thread_sendpkt(void *arg){/*PROBLEMA: scrivere su un contatore globale di npkt posizioni*/
  void *status;
	struct elab2 *cargo;
	cargo = (struct elab2 *)arg;

	printf("sono il thread # %d \n",(cargo->thpkt->seq)-(cargo->initialseq));
	printf("valore : %d \n" ,cargo->p[(cargo->thpkt->seq)-(cargo->initialseq)]);

	cargo->p[(cargo->thpkt->seq)-(cargo->initialseq)] =(int *) 8;

	printf("valore : %d \n",cargo->p[(cargo->thpkt->seq)-(cargo->initialseq)]);
	pthread_exit(status);

}

void list(char** res, const char* path){
    char command[DATASIZE];
    FILE* file;

    sprintf(command, "ls %s | cat > list.txt", path);
    system(command);

    file = fopen("list.txt", "r");
    fread(*res, DATASIZE, 1, file);
}

int get(int iseq,int iack, int numpkt, char * filename){ //iseq=11,iack=31,numpkt=10,filename="pluto.jpg"
//  int sockid;
  struct pkt* ack;
  struct pkt *pktget; //array di pkt da inviare
  struct elab2 **sendpkt;
  int fd;
  int j,z;
  pthread_t tid;
  void **status;
	int **counter;
  int aux;
  void * dati;
  int init= iseq;

  //  setsock();
  ack = (struct pkt *)check_mem(malloc(sizeof(struct pkt *)), "GET-server:malloc ack")
  check(recvfrom(sockd,ack, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len), "GET-server:recvfrom ack-client");
  if(strcmp(ack->data, "ok")==0){   /* ho ricevuto ack positivo dal client */
    printf("[SERVER] Connection established \n");
    fd = open((char *)filename,O_RDONLY,00700); //apertur
    if (fd == -1){    /*file non aperto correttamente*/
      printf("[SERVER] Problem opening file %s \n",  filename);
      exit(EXIT_FAILURE);
    }
/*    if ((shm_ds = shmget(shm_key, shmsize, 0)) == -1) { /*creazione mem.condivisa*
      printf("Errore. Impossibile reperire la memoria condivisa.\n");
      return 1;
    }
    if ((shm_addr = shmat(shm_ds, NULL, 0)) == (void *) -1) { /*aggancio mem.condivisa*
		    printf("Errore. Impossibile agganciare la memoria condivisa.\n");
		return -1;
  }*/
transfer:
    sendpkt = malloc((numpkt)*sizeof(struct elab2)); /*Alloca la memoria per thread che eseguiranno la get */
    if(sendpkt == NULL){
      printf("server: ERRORE malloc sendpkt del file %s",filename);
      exit(EXIT_FAILURE);
    }

    counter = malloc((numpkt)*sizeof(int *));
    if(counter == NULL){
      printf("server: errore malloc contatore ack \n");
      exit(EXIT_FAILURE);
    }
    for(z=0; z<numpkt; z++){
		  counter[z] = 0;         //inizializza a 0 il counter
	  }

    pktget = (struct pkt *)malloc(sizeof(struct pkt));  //alloco memoria per un pkt


    /*pktget =  malloc((numpkt)*sizeof(struct pkt)); //Alloca la memoria per thread che eseguiranno la get
    if(pktget == NULL){
      printf("server: ERRORE malloc pktget del file %s",filename);
      exit(EXIT_FAILURE);
    }*/
    for(j=0;j<numpkt;j++){
      aux = readn(fd,dati,DATASIZE);
      pktget = makepkt(5,iseq,0,numpkt-j,dati);
    /*  pktget.size = (int) readn(fd,pktget.data,DATASIZE);
      pktget.op = 5;   //cargo
      pktget.seq = iseq;
      pktget.ack = 0;
      pktget.pktleft = numpkt-j;  //da rivedere in caso di reinvii
      pktget.data = */

      sendpkt[j] =(struct sendpkt_struct *) mmap(NULL,(sizeof(struct sendpkt_struct)), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS ,0,0); /*mmap sendpkt*/
      if (sendpkt[j] == NULL){
        printf("%s",strerror(errno));
      }
      sendpkt[j]->thpkt = pktget;
	    sendpkt[j]->p = counter;
	    sendpkt[j]->initialseq = init;

      iseq++;
      if(pthread_create(&tid,NULL,thread_sendpkt,(void *)sendpkt[j])!=0){
        printf("server:ERRORE pthread_create GET in main");
        exit(EXIT_FAILURE);
      }
      for (int j = 0; j < numpkt; j++) {
        pthread_join(tid,status);
      }
    }
  } // else other statuses
  return 0; /* ho ricevuto ack negatico dal client */
}


int put(struct pkt *pkt, int filesize){
	int ret; // for returning values
    char *sndbuf;
    struct pkt *cpacket;

	char *status = "ok";
	cpacket = pkt;
	setrcvputsock();
	sendack(rcvputsockd, cpacket->seq, filesize, status);
printf("[Server] Sending ACK for connection for put operation to client #%d...\n\n", cliaddr.sin_port);
}


int main(int argc, char const* argv[]) {
    char *spath = DEFAULT_PATH; // root folder for server
    struct pkt *cpacket;
    char *filename;

    /* Usage */
    if(argc > 2){
        fprintf(stderr, "Path from argv[1] set, extra parameters are discarded. [Usage]: %s [<path>]\n", argv[0]);
    }

    /* Init */
    if(argc > 1) spath = (char *)argv[1];
printf("[Server] Root folder: %s\n", spath);
    nextseqnum = 0;
    setsock();
    cpacket = (struct pkt *)check_mem(malloc(sizeof(struct pkt)), "main:malloc:cpacket");
    len = sizeof(cliaddr);

    // TMP for testing list
    char *res = malloc((DATASIZE-1) * sizeof(char)); // client has to put \0 at the end
    char **resptr = &res;
    // TMP for testing ack status
    int filesize;
    int listsize = 0;

    while(1){
        /* Infinite receiving  */
        check_mem(memset((void *)&cliaddr, 0, sizeof(cliaddr)), "main:memset");
        check(recvfrom(sockd, cpacket, HEADERSIZE+DATASIZE, 0, (struct sockaddr *)&cliaddr, &len), "main:rcvfrom");
printf("[Server] Received [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", cpacket->op, cpacket->seq, cpacket->ack, cpacket->pktleft, cpacket->size, cpacket->data);

        /* Operation selection */
        switch (cpacket->op) {
            case 1: // list
                sendack(sockd, cpacket->seq, listsize, "ok");

                // TMP for testing list
                list(resptr, spath);
                check(sendto(sockd, (char *)res, strlen(res), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr)) , "main:sendto");

printf("[Server] Sending list to client #%d...\n\n", cliaddr.sin_port);
                break;
            case 2: // get
                 // calculate the size of the arg file
                strncpy(filename,cpacket->data,sizeof(cpacket->data)); /* salvo il filename del file richiesto*/
                printf("1 print filename: %s",cpacket->data); //prova
                printf("1 print filename: %s",filename);
                filesize = calculate_numpkts(filename);
                if (filesize == -1) {
                  sendack(sockd, cpacket->seq, filesize, "GET:File non presente");
        				}
                else{
                  printf("[SERVER] File selected is %s and it has generate %d pkt to transfer \n", filename, filesize);
                  sendack(sockd, cpacket->seq, filesize, "ok");
                  if(get(nextseqnum,cpacket->seq, filesize, filename)){
                    printf("[SERVER] Sending file %s complete with success \n", filename);
                  }
                  else {
                    printf("[SERVER]Problem with transfer file %s to server  \n",filename);
                  	exit(EXIT_FAILURE);
                  }
                }
                printf("My job here is done\n\n");
                break;
            case 3: // put
            	if(freespacebuf(cpacket->pktleft)){
		        	if(put(cpacket, cpacket->pktleft)) {
		        	//sendack(cpacket->seq, 0, status);
	printf("My job here is done\n\n");
					};
            	} else{
    printf("[Server] Can't handle this packet, no space for the file\n\n");
    			sendack(sockd, cpacket->seq, 0, "fullbuf");
            	}
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
