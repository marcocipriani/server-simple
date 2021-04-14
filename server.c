#include "common.c"
#include "config.h"

int sockd;
int nextseqnum;
char *msg;
char rcvbuf[45000]; //buffer per la put
struct sockaddr_in cliaddr;
socklen_t len;
void **status;

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

void sendack(int sockd, struct pkt * ack){
/*void sendack(int sockd, int cliseq, int pktleft, char *status){ //aggiunto sockd per specificare quale socket invia l'ack
    struct pkt *ack;

    nextseqnum++;
    ack = (struct pkt *)check_mem(makepkt(4, nextseqnum, cliseq, pktleft, status), "sendack:makepkt");
	*/
    check(sendto(sockd, ack, (HEADERSIZE)+ack->size, 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr)) , "sendack:sendto");
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


void *thread_sendpkt(void *arg){ /*PROBLEMA: scrivere su un contatore globale di npkt posizioni*/
  //void *status;
  struct elab2 *cargo;
  struct pkt *sndpkt,*rcvack;
  int me;
  char supp[DATASIZE];
  cargo = (struct elab2 *)arg;
  me = (cargo->thpkt.seq)-(cargo->initialseq);   //numero thread
  //printf("cargo->thpkt->data %s \n",cargo->thpkt->data);
  //memcpy(supp,cargo->thpkt->data,cargo->thpkt->size);
  sndpkt = check_mem((struct pkt *)malloc(sizeof(struct pkt)),"GET: sndpkt malloc");
  check(makepkt(sndpkt, 5, cargo->thpkt.seq, 0, cargo->thpkt.pktleft, cargo->thpkt.data, cargo->thpkt.size), "SERVER-get-thread: sndpkt");
	//printf("cargo->thpkt->seq: %d \n",cargo->thpkt.seq);
	//printf("cargo->initialseq: %d \n",cargo->initialseq);
	printf("sono il thread # %d \n",me);
	printf("valore del counter[%d] : %d \n" ,me,cargo->p[me]);
	//printf("sendpkt data: %s \n",sndpkt->data);
	//printf("cargo->thpkt->data %s \n",cargo->thpkt.data);
	//printf("arg->thpkt->data %s \n",arg->thpkt->data);
	//cargo->p[(cargo->thpkt->seq)-(cargo->initialseq)] =(int *) 8


  sendto(sockd, sndpkt, HEADERSIZE+sndpkt->size, 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
check_ack:

  rcvack = (struct pkt *)check_mem(malloc(sizeof(struct pkt)), "SERVER-get-thread: malloc rcvack");
  check(recvfrom(sockd,rcvack, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len), "SERVER-get-thread:recvfrom ack-client");
  //lock buffer
  printf("sono il thread # %d e' ho ricevuto l'ack del pkt #%d \n",me,(rcvack->ack)-(cargo->initialseq)+1);
  //printf("cargo->p[(rcvack->ack) : %d \n", (rcvack->ack));
  //printf("INITIAL SEQ : %d \n", cargo->initialseq);
  printf("valore di partenza in counter[%d] : %d \n",(rcvack->ack)-(cargo->initialseq),cargo->p[(rcvack->ack)-(cargo->initialseq)]);


  if((cargo->p[(rcvack->ack)-(cargo->initialseq)])==0){
    cargo->p[(rcvack->ack)-(cargo->initialseq)]=(int) cargo->p[(rcvack->ack)-(cargo->initialseq)]+ 1; /*aumento #ack di 1*/
    printf("valore aggiornato in counter[%d] : %d \n",(rcvack->ack)-(cargo->initialseq),cargo->p[(rcvack->ack)-(cargo->initialseq)]);
    //unlock buffer

    printf("sono il thread # %d e muoio \n",me);
    pthread_exit(status);
  }
  else if((cargo->p[(rcvack->ack)-(cargo->initialseq)])==2){
    printf("dovrei fare una fast retransmit del pkt con #seg: %d/n",rcvack->ack );
    (cargo->p[(rcvack->ack)-(cargo->initialseq)])=(cargo->p[(rcvack->ack)-(cargo->initialseq)])+ 1;
    printf("valore aggiornato in counter[%d] : %d \n",(rcvack->ack)-(cargo->initialseq),cargo->p[(rcvack->ack)-(cargo->initialseq)]);
    //unlock buffer
    goto check_ack;
  }
  else{
    (cargo->p[(rcvack->ack)-(cargo->initialseq)])=(cargo->p[(rcvack->ack)-(cargo->initialseq)])+ 1;
    printf("SONO IMPAZZITO \n");
    printf("valore aggiornato in counter[%d] : %d \n",(rcvack->ack)-(cargo->initialseq),cargo->p[(rcvack->ack)-(cargo->initialseq)]);
    //unlock buffer
    goto check_ack;
  }

  /*buffer[num_threads]

thread[i]:
    initialseqnumb = 50;
	mynumb = 50+i;
    sendto(cargo seq = mynumb);

do:
    rcvfrom(sock, pack)
    --- lock(buffer)
    if (buffer[pack.ack-initialseqnumb] =3){
		-- unlock
		sendto(cargo seq = mynumb+1)
		goto do;
	} else if(buffer[pack.ack-initialseqnumb] = 0){
		write buffer[pack.ack-initialseqnumb] = +1;
		exit;
	} else {
		write buffer[pack.ack-initialseqnumb] = +1;
	}
    --- unlock
	goto do;
	*/

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
  //struct pkt *pktget; //pkt da inviare di supporto
  struct elab2 **sendpkt;
  int fd;
  int i,j,k,z;
  pthread_t *tid;
  //void **status;
  int **counter;
  int aux;
  char *dati;
  int init= iseq;

  //  setsock();
  ack = (struct pkt *)check_mem(malloc(sizeof(struct pkt)), "GET-server:malloc ack");
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
    printf("inizio trasferimento \n");
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

    tid = malloc((numpkt)*sizeof(pthread_t));
    if(tid == NULL){
      printf("server: errore malloc contatore ack \n");
      exit(EXIT_FAILURE);
    }

    for(z=0; z<numpkt; z++){
		  counter[z] = 0;         //inizializza a 0 il counter
    }
	
 /*   pktget = (struct pkt *)malloc(sizeof(struct pkt));  //alloco memoria per un pkt
    if(pktget == NULL){
      printf("server: ERRORE malloc pktget del file %s",filename);
      exit(EXIT_FAILURE);
    } */

    dati= (char*)malloc(DATASIZE);
    for(j=0;j<numpkt;j++){
   	printf("entro nel for \n");
      aux = readn(fd,dati,DATASIZE);
      printf("aux %d \n",aux);
      printf("lunghezza dati: %d \n",strlen((char *)dati));
   /*   for(z=0; z<120; z++){
        printf("%c",dati[z]);
      }*/
      //printf("dati[315] %c \n",dati[315]);
     // printf("init+j+1 %d\n",init+j+1);
     // printf("iseq +1: %d\n",iseq +1);
      
    //  check(makepkt(pktget,5,iseq,0,numpkt-j,dati),"GET:pktget makepkt");
   //   printf("pktget size %d, pktget left %d, pktget dati %s \n",pktget->size, pktget->pktleft,pktget->data);
    /*  pktget.size = (int) readn(fd,pktget.data,DATASIZE);
      pktget.op = 5;   //cargo
      pktget.seq = iseq;
      pktget.ack = 0;
      pktget.pktleft = numpkt-j;  //da rivedere in caso di reinvii
      pktget.data = */

      sendpkt[j] =(struct elab2 *) mmap(NULL,(sizeof(struct elab2)), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS ,0,0); /*mmap sendpkt*/
      if (sendpkt[j] == NULL){
        printf("%s",strerror(errno));
      }

      check(makepkt(&(sendpkt[j]->thpkt),5,iseq,0,numpkt-j,dati,aux),"GET:senpkt[j] makepkt");
      printf("(sendpkt[%d] SIZE %d, pktleft %d, dati %s \n",j, sendpkt[j]->thpkt.size, sendpkt[j]->thpkt.pktleft,sendpkt[j]->thpkt.data);
      //sendpkt[j]->thpkt = pktget;
      sendpkt[j]->p = counter;
      sendpkt[j]->initialseq = init;
      for(z=0; z<120; z++){
        printf("%c",sendpkt[j]->thpkt.data[z]);
      }

      //printf("pktget->seq: %d \n",pktget->seq);
      //printf("sendpkt[%d]->thpkt.seq: %d \n",j,sendpkt[j]->thpkt.seq);
     // printf("dati: %s \n",dati);
     // printf("pktget->data %s \n",pktget->data);
     // printf("sendpkt[%d]->thpkt.data: %s \n",j, sendpkt[j]->thpkt.data);

      //sendto(sockd,(void *)&(sendpkt[j]->thpkt), HEADERSIZE+strlen(sendpkt[j]->thpkt.data), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
      if(pthread_create(&tid[j],NULL,thread_sendpkt,(void *)sendpkt[j])!=0){
        printf("server:ERRORE pthread_create GET in main");
        exit(EXIT_FAILURE);
      }
      memset(dati,0,DATASIZE);
      //memset(pktget->data,0,pktget->size+1);
      //pktget->seq = 0;
      //pktget->pktleft=0;
      //printf("j= %d e numpkt= %d \n",j,numpkt);
      iseq++;
    }
    //sleep(2);
    for ( k = 0; k <numpkt; k++) {
      //printf("k= %d e numpkt= %d \n",k,numpkt);
      printf("sono il padre e aspetto %d thread \n", numpkt-k);
      //sleep(2);
      pthread_join(tid[k],status);
      printf("un figlio e' morto \n");
    }
    printf("tutti i thread hanno finito \n");
    //controllo che siano stati ricevuti tutti gli ACK
    for (i=0; i<numpkt; i++){
    	printf("counter[%d]: %d \n",i, counter[i]);
      if(counter[i]==(int *)0){
        printf("errore nell'invio/ricezione del pkt/ack: %d \n", i);
        return 0;
      }
    }
    return 1;
  }
  else{
    printf("il client rifiuta il trasferimento del file/n");
    return 0; /* ho ricevuto ack negativo dal client */
  }

}


/*int put(struct pkt *pkt, int filesize){
	int ret; // for returning values
    char *sndbuf;
    struct pkt *cpacket;

	char *status = "ok";
	cpacket = pkt;
	sendack(sockd, cpacket->seq, filesize, status);
printf("[Server] Sending ACK for connection for put operation to client #%d...\n\n", cliaddr.sin_port);
}*/


int main(int argc, char const* argv[]) {
    char *spath = DEFAULT_PATH; // root folder for server
    struct pkt *cpacket, *sack;
    char filename[50];

    /* Usage */
    if(argc > 2){
        fprintf(stderr, "Path from argv[1] set, extra parameters are discarded. [Usage]: %s [<path>]\n", argv[0]);
    }

    /* Init */
    if(argc > 1) spath = (char *)argv[1];
printf("[Server] Root folder: %s\n", spath);
    nextseqnum = 1;
    setsock();
    cpacket = (struct pkt *)check_mem(malloc(sizeof(struct pkt)), "main:malloc:cpacket");
    len = sizeof(cliaddr);
    sack = (struct pkt *)check_mem(malloc(sizeof(struct pkt)),"sack: malloc");

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
                check(makepkt(sack, 4, nextseqnum, cpacket->seq, listsize, "ok",2),"sack: makepkt");
                sendack(sockd, sack);
		memset(sack->data,0,sack->size+1);
                // TMP for testing list
                list(resptr, spath);
                check(sendto(sockd, (char *)res, strlen(res), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr)) , "main:sendto");

printf("[Server] Sending list to client #%d...\n\n", cliaddr.sin_port);
                break;
            case 2: // get
                 // calculate the size of the arg file
                printf("filename: %s \n",cpacket->data); //prova
                printf("lunghezza filename: %d \n",cpacket->size);
                strncpy(filename,cpacket->data,cpacket->size+1); /* salvo il filename del file richiesto*/
                //printf("filename: %s \n",cpacket->data); //prova
                printf("filename copiato: %s \n",filename);
                filesize = calculate_numpkts(filename);
                if (filesize == -1) {
                  check(makepkt(sack, 4, nextseqnum, cpacket->seq, 0, "GET: File non presente",22),"snack: makepkt");
                  sendack(sockd, sack);
                  memset(sack->data,0,sack->size+1);
                  //GOTO while1
        	}
                else{
                  printf("[SERVER] File selected is %s and it has generate %d pkt to transfer \n", filename, filesize);
                  check(makepkt(sack, 4, nextseqnum, cpacket->seq, filesize, "ok",2),"sack: makepkt");
                  sendack(sockd, sack);
                  memset(sack->data,0,sack->size+1);
                  if(get(nextseqnum,cpacket->seq, filesize, filename)){
                    printf("[SERVER] Sending file %s complete with success \n", filename);
                  }
                  else {
                    printf("[SERVER]Problem with transfer file %s to server  \n",filename);
                  	exit(EXIT_FAILURE);
                  }
                }
                printf("My job here is done\n\n");
                memset(cpacket->data,0,cpacket->size+1);
                break;
            /*case 3: // put
            	if(freespacebuf(cpacket->pktleft)){
		        	if(put(cpacket, cpacket->pktleft)) {
		        	//sendack(cpacket->seq, 0, status);
	printf("My job here is done\n\n");
					};
            	} else{
    printf("[Server] Can't handle this packet, no space for the file\n\n");
    			sendack(sockd, cpacket->seq, 0, "fullbuf");
            	}
                break;*/
            default:
printf("[Server] Can't handle this packet\n\n");
                // SEND A NACK? to protect from wrong packets
              //  sendack(sockd, cpacket->seq, 0, "malformed packet");
                break;
        }
    }

    exit(EXIT_FAILURE);
}
