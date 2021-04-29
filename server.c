#include "common.c"
#include "macro.h"

int SemSnd_Wndw;
int sockd; // global until setop calls for setsock
struct sockaddr_in servaddr, cliaddr;
socklen_t len;
int nextseqnum, initseqserver;
char *msg;
char rcvbuf[45000]; // buffer per la put
void **tstatus;
char *status = "okclient";
char *spath = SERVER_FOLDER; // root folder for server

void sendack(int sockd, int op, int cliseq, int pktleft, char *status) {
    struct pkt ack;

	check_mem(memset(ack.data, 0, ((DATASIZE) * sizeof(char))), "main:memset:cpacket");
    nextseqnum++;
    ack = makepkt(op, nextseqnum, cliseq, pktleft, strlen(status), status);

    check(sendto(sockd, &ack, HEADERSIZE + ack.size, 0, (struct sockaddr *)&cliaddr, sizeof(struct sockaddr_in)), "sendack:sendto");
printf("[Server] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);

}

/*
 *  function: waitforack
 *  ----------------------------
 *  Wait for a positive ack
 *
 *  return: 1 on successfully received ack
 *  error: 0
 */
int waitforack() {
    struct pkt ack;

printf("[Server] Waiting for ack...\n");
    check(recvfrom(sockd, &ack, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len), "waitforack:recvfrom");
printf("[Server] Received ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, ack.data);

    if (ack.op == ACK_POS) {
        return 1;
    }
    return 0;
}

int freespacebuf(int totpkt) {
    size_t totpktsize;
    int res;

    totpktsize = (size_t)(totpkt * sizeof(char)) * (DATASIZE * sizeof(char));
    res = sizeof(rcvbuf) - totpktsize;
    if (res >= 0) {
    return 1;
    }else{
    return 0;
    }
}

//input:puntatore a pila,sem(pkts_to_send),ack_counters,base,nextseqnum,initialseq(not required if pktlft->seq relative),sockd
//timer, estimatedRTT..pid padre
void *thread_sendpkt(void *arg) {
    struct thread_info *cargo;//t_info
    struct pkt sndpkt, rcvack;
    //int me;
    int k,n;
    union sigval retransmit_info;
    struct sembuf oper;
    cargo = (struct thread_info *)arg;
/*    me = (cargo->thpkt.seq) - (cargo->initialseq); // numero thread
    sndpkt = makepkt(5, cargo->thpkt.seq, 0, cargo->thpkt.pktleft, cargo->thpkt.size, cargo->thpkt.data);
printf("sono il thread # %d \n", me);
printf("valore del counter[%d] : %d \n", me, cargo->p[me]);*/
transmit:
    oper.sem_num = 0;
    oper.sem_op = -1;
    oper.sem_flag = SEM_UNDO;

    check(semop(cargo->semLoc,&oper,1),"THREAD: error wait semLoc");    //wait su semLoc

    oper.sem_num = 0;
    oper.sem_op = -1;
    oper.sem_flag = SEM_UNDO;

    check(semop(cargo->semLoc,&oper,1),"THREAD: error wait global");    //wait su semGlob

    check(pthread_mutex_lock(&cargo->mutex_stack),"THREAD: error lock Stack");      //lock sulla Pila
    sndpkt = pop((struct CellaPila)&cargo->stack);
    if (sndpkt==NULL){
        check(pthread_mutex_unlock(&cargo->mutex_stack),"THREAD: error unlock Stack");//unlock pila

        oper.sem_num = 0;
        oper.sem_op = 1;
        oper.sem_flag = SEM_UNDO;

        check(semop(SemSnd_Wndw,&oper,1),"THREAD: error signal global");    //signal a semGlobal

        //serve la wait a semLocale??
        pthread_exit(NULL);
    }
    oper.sem_num = 0;                                                 //se pop a buon fine
    oper.sem_op = 1;                                                  //signal a semGlobal
    oper.sem_flag = SEM_UNDO;

    check(semop(SemSnd_Wndw,&oper,1),"THREAD: error signal global");

    check(pthread_mutex_unlock(&cargo->mutex_stack),"THREAD: error unlock Stack");   //unlock Pila

    sendto(sockd, &sndpkt, HEADERSIZE + sndpkt.size, 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
    //nextseqnum++;//LA RITRASMISSIONE NON DEVE ALZARLO
    //lock timer
    if (cargo->timer == 0){
        timer=1;
      //avvia sampleRTT
      oper.sem_num = 0;
      oper.sem_op = 1;                                                  //signal a semTimer
      oper.sem_flag = SEM_UNDO;

      check(semop(semTimer,&oper,1),"THREAD: error signal semTimer");


check_ack:
    n=recvfrom(sockd, &rcvack, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len);

    check(pthread_mutex_lock(&cargo->mutex_ack_counter),"THREAD: error lock Ack Counters");
printf("sono il thread # %d e' ho ricevuto l'ack del pkt #%d \n", me, (rcvack.ack) - (cargo->initialseq) + 1);
printf("valore di partenza in counter[%d] : %d \n", (rcvack.ack) - (cargo->initialseq), cargo->p[(rcvack.ack) - (cargo->initialseq)]);

    if(n>0){
      if(rcvack.ack-base>WSIZE || rcvack.ack<base-1){    //ack fuori finestra
        check(pthread_mutex_unlock(&cargo->mutex_ack_counter),"THREAD: error unlock Ack Counters");
        goto check_ack;
      }
      if(rcvack.ack >= base){   //ricevo un ack nella finestra
          for (k=base;k<=rcvack.ack;k++){
            //se pktlft=seq relative..da fare
            cargo->ack_counters[k - (cargo->initialseq)] = (int)cargo->ack_counters[k - (cargo->initialseq)] + 1; //sottraggo il num.seq iniziale
            cargo->base++; //da controllare

            oper.sem_num = 0;                                                 //se ack in finestra
            oper.sem_op = 1;                                                  //signal a semGlobal
            oper.sem_flag = SEM_UNDO;

            check(semop(SemSnd_Wndw,&oper,1),"THREAD: error signal global at received ack ");
  printf("valore aggiornato in counter[%d] : %d \n", k - (cargo->initialseq), cargo->ack_counters[k - (cargo->initialseq)]);
          }
          check(pthread_mutex_unlock(&cargo->mutex_ack_counter),"THREAD: error unlock Ack Counters");
          if(rcvack.ack == cargo->initialseq+cargo->numpkt){//if(rcvack.pktlft==0)
  printf("GET:Ho riscontrato l'ultimo ack del file\n");
              kill(father_pid,SIGLASTACK);  //finito il file-il padre dovrà mandare un ack
              pthread_exit(NULL);
          }
            goto transmit;
      }
      else if(rcvack.ack==base-1){    //ack duplicato
          if ((cargo->ack_counters[(rcvack.ack) - (cargo->initialseq)]) == 2) {
  printf("dovrei fare una fast retransmit del pkt con #seg: %d/n", rcvack.ack);
              (cargo->ack_counters[(rcvack.ack) - (cargo->initialseq)]) = 0;//(cargo->p[(rcvack.ack) - (cargo->initialseq)]) + 1;
  printf("azzero il counter[%d] : %d \n", (rcvack.ack) - (cargo->initialseq), cargo->ack_counters[(rcvack.ack) - (cargo->initialseq)]);

              check(pthread_mutex_unlock(&cargo->mutex_ack_counter),"THREAD: error unlock Ack Counters");
              retransmit_info.sival_int = (rcvack.ack) - (cargo->initialseq)+1;
              sigqueue(cargo->father_pid, SIGRETRANSMIT, retransmit_info);
          }
          else {
              (cargo->ack_counters[(rcvack.ack) - (cargo->initialseq)])=(int)(cargo->ack_counters[(rcvack.ack) - (cargo->initialseq)])+1;
              check(pthread_mutex_unlock(&cargo->mutex_ack_counter),"THREAD: error unlock Ack Counters");
              goto check_ack;
          }
      }
    }
    //se non ho ricevuto niente da rcvfrom
    if(cargo->ack_counters[sendpkt.seq-initialseq/*pktleft if setted to seq_relative*/]>0){ //se il pkt che ho inviato è stato ackato trasmetto uno nuovo
      check(pthread_mutex_unlock(&cargo->mutex_ack_counter),"THREAD: error unlock Ack Counters");
      goto transmit;
    }
    else{       //se il mio pkt non è stato ackato continuo ad aspettare l'ack
      check(pthread_mutex_unlock(&cargo->mutex_ack_counter),"THREAD: error unlock Ack Counters");
      goto check_ack;
    }
}

int get(int iseq, int numpkt, char *filename) { // iseq=11,iack=31,numpkt=10,filename="pluto.jpg"
    struct pkt ack;
    struct elab2 *sendpkt;
    int fd;
    int i, j, k, z;
    pthread_t *tid;
    int *counter;
    int aux,oldBase;
    char *dati;
    pthread_mutex_t mtxStack,mtxAck_counter;
    int semTimer,semPkt_to_send;
    struct thread_info t_info;
    pid_t pid;
    int init = iseq;
    int base = init;
    int *timer;
    double *estimatedRTT, *timeout_Interval;
    struct timespec *startRTT;

	  check_mem(memset(ack.data, 0, ((DATASIZE) * sizeof(char))), "GET-server:memset:ack-client");
    check(recvfrom(sockd, &ack, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len), "GET-server:recvfrom ack-client");
printf("[Server] Received ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, ack.data);
    if (strcmp(ack.data, "ok") == 0) { /* ho ricevuto ack positivo dal client */
printf("[SERVER] Connection established \n");

        pid = getpid();
        CellaPila stackPtr = NULL;
        *timeout_Interval=TIMEINTERVAL;

        fd = open(filename, O_RDONLY, 00700); // apertura file da inviare
        if(fd == -1){ /*file non aperto correttamente*/
printf("[SERVER] Problem opening file %s \n", filename);
            exit(EXIT_FAILURE);
        }

printf("inizio trasferimento \n");
        sendpkt = malloc((numpkt) * sizeof(struct pkt)); /*Alloca la memoria per thread che eseguiranno la get */
        if(sendpkt == NULL){
printf("server: ERRORE malloc sendpkt del file %s", filename);
            exit(EXIT_FAILURE);
      }

        counter = malloc((numpkt) * sizeof(int));
        if(counter == NULL){
printf("server: errore malloc contatore ack \n");
            exit(EXIT_FAILURE);
        }

        tid = malloc((WSIZE) * sizeof(pthread_t));
        if (tid == NULL) {
printf("server: errore malloc contatore ack \n");
            exit(EXIT_FAILURE);
        }

        for (z = 0; z < numpkt; z++) {
            counter[z] = 0; // inizializza a 0 il counter
        }

        dati = (char *)malloc(DATASIZE);
        for (j = 0; j < numpkt; j++) {
            aux = readn(fd, dati, DATASIZE);
printf("aux %d \n", aux);
printf("lunghezza dati: %lu\n", strlen((char *)dati));

            sendpkt[j] = makepkt(5, iseq, 0, numpkt - j, aux, dati);
printf("(sendpkt[%d] SIZE %d, pktleft %d, dati %s \n", j, sendpkt[j].size, sendpkt[j].pktleft, sendpkt[j].data);
            /*sendpkt[j].p = counter;
            sendpkt[j].initialseq = init;*/
          /*  for (z = 0; z < 120; z++) {
printf("%c", sendpkt[j].thpkt.data[z]);
}*/         memset(dati, 0, DATASIZE);
            iseq++;
        }

            for (z=numpkt-1; z>=0;z--){
              push(&stackPtr, sendpkt[z]);
            }

            check(pthread_mutex_init(&mtxStack,NULL),"GET: errore pthread_mutex_init Pila");

            check(pthread_mutex_init(&mtxAck_counter,NULL),"GET: errore pthread_mutex_init Ack Counters");

            semTimer=check(semget(IPC_PRIVATE,1,IPC_CREAT|IPC_EXCL|0666),"GET: semget semTimer"); //inizializzazione SemTimer
            check(semctl(semTimer,0,SETVAL,0), "GET: semctl semTimer");

            semPkt_to_send=check(semget(IPC_PRIVATE,1,IPC_CREAT|IPC_EXCL|0666),"GET: semget semPkt_to_send"); //inizializzazione semPkt_to_send
            check(semctl(semPkt_to_send,0,SETVAL,numpkt), "GET: semctl semPkt_to_send");

            //preparo il t_info da passare ai thread

            t_info.stack = stackPtr;
            t_info.semLoc = semPkt_to_send;
            t_info.semTimer = semTimer;
            t_info.mutex_stack = mtxStack;
            t_info.mutex_ack_counter = mtxAck_counter;
            t_info.ack_counters = counter;
            t_info.base = &base;
            t_info.initialseq = init;
            t_info.numpkt = numpkt;
            t_info.sockid = sockd; //per ora
            t_info.timer = timer;
            t_info.estimatedRTT = estimatedRTT;
            t_info.start = &startRTT;
            //t_info.end = endRTT;
            t_info.timeout_Interval = &timeout_Interval;
            t_info.father_pid = pid;



            for(j=0;j<WSIZE;j++)
            if(pthread_create(&tid[j], NULL, thread_sendpkt, (void *)stackPtr) != 0){
printf("server:ERRORE pthread_create GET in main");
                exit(EXIT_FAILURE);
            }


          signal(SIGRETRANSMIT,push_base);//usa sigaction
          //signal(/*stop timer-base aggiornata*/);
          while((base-initialseq)<=numpkt){
            oper.sem_num = 0;
            oper.sem_op = -1;
            oper.sem_flag = SEM_UNDO;

            check(semop(semTimer,&oper,1),"THREAD: error wait semTimer");   //WAIT su semTimer

            int oldBase=base;
            usleep(*timeout_Interval);
            if (counter[oldBase - initialseq]==0) //if (control==base)   //RITRASMISSIONE
              push(&stackPtr,send[control - initialseq])  //o handler()signal(sem_ppkts_to_send)

              oper.sem_num = 0;                                                 //se RITRASMISSIONE
              oper.sem_op = 1;                                                  //signal a semPkt_to_send
              oper.sem_flag = SEM_UNDO;

              check(semop(semPkt_to_send,&oper,1),"GET: error signal semLocal ");

              //lock timer
              *timer=0;
              //unlock timer
          }


        // controllo che siano stati ricevuti tutti gli ACK
        for(i = 0; i < numpkt; i++){
printf("counter[%d]: %d \n", i, counter[i]);
            if(counter[i] == 0){
printf("errore nell'invio/ricezione del pkt/ack: %d \n", i);
                return 0;
            }
        }

        return 1;
    }else{
printf("il client rifiuta il trasferimento del file\n");
        return 0; /* ho ricevuto ack negativo dal client */
    }
}

int put(int iseq, void *pathname, int pktleft){

    int fd;
    size_t filesize;
    int npkt,edgepkt;
    int pos,lastpktsize;
    char *localpathname;
    struct pkt rack, cargo;

    npkt = pktleft;
    edgepkt = npkt;
    check(recvfrom(sockd, &rack, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len), "PUT-server:recvfrom ack-client");
printf("[Server] Received ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", rack.op, rack.seq, rack.ack, rack.pktleft, rack.size, rack.data);

    if (strcmp(rack.data, "ok") == 0) {
    	initseqserver = rack.seq;
    	localpathname = malloc(DATASIZE * sizeof(char));
    	sprintf(localpathname, "%s%s", SERVER_FOLDER, pathname);

receiver:
        while(npkt>0){
            check(recvfrom(sockd,&cargo, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len), "PUT-server:recvfrom Cargo");
printf("[Server] pacchetto ricevuto: seq %d, ack %d, pktleft %d, size %d, data %s \n", cargo.seq, cargo.ack, cargo.pktleft, cargo.size, cargo.data);
            pos=(cargo.seq - initseqserver);
			if(pos>edgepkt && pos<0){
printf("[Server] numero sequenza pacchetto ricevuto fuori range \n");
                return 0;
            } else if((rcvbuf[pos*(DATASIZE)])==0){ // sono nell'intervallo corretto
            	printf("VALORE PACCHETTO %d \n",(initseqserver+edgepkt-1));
                if(cargo.seq == (initseqserver+edgepkt-1)){
                    lastpktsize = cargo.size;
                }
                memcpy(&rcvbuf[pos*(DATASIZE)],cargo.data,DATASIZE);
                sendack(sockd, ACK_POS, cargo.seq, cargo.pktleft, "ok");
printf("[Server] il pacchetto #%d e' stato scritto in pos:%d del buffer\n",cargo.seq,pos);
            }else{
            	printf("[Server] pacchetto già ricevuto, posso scartarlo \n");
                sendack(sockd, ACK_POS, cargo.seq, cargo.pktleft, "ok");
                goto receiver; // il pacchetto viene scartato
            }
            npkt--;
        }
        filesize = (size_t)((DATASIZE)*(edgepkt-1))+lastpktsize; //dimensione effettiva del file
        fd = open(localpathname,O_RDWR|O_TRUNC|O_CREAT,0666);
        writen(fd,rcvbuf,filesize);
printf("[Server] il file %s e' stato correttamente scaricato\n",(char *)pathname);
		memset(rcvbuf, 0, (size_t)((DATASIZE)*(edgepkt-1))+lastpktsize);
        return 1;
    } else {
    printf("[Server] Client refuses to transfer the file \n");
    return 0;
    }
}

/*
 *  function: makelist
 *  ----------------------------
 *  Print list on
 *
 *  res: pointer to string where the result is stored
 *  path: folder to list
 *
 *  return: -
 *  error: -
 */
void makelist(char **res, const char *path) {
    char command[DATASIZE];
    FILE *file;

    sprintf(command, "ls %s | cat > list.txt", path);
    system(command);

    file = fopen("list.txt", "r");
    fread(*res, DATASIZE, 1, file);
}

/*
 *  function: managelist
 *  ----------------------------
 *  Execute makelist function and send the result to the client
 *
 *  return: -
 *  error: -
 */
void managelist() {
    char *res = malloc(((DATASIZE)-1)*sizeof(char)); // client has to put \0 at the end
    char **resptr = &res;
    struct pkt listpkt;

    makelist(resptr, spath);
    listpkt = makepkt(CARGO, nextseqnum, 1, 1, strlen(res), res);
printf("[Server] Sending list [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", listpkt.op, listpkt.seq, listpkt.ack, listpkt.pktleft, listpkt.size, (char *)listpkt.data);
    check(sendto(sockd, &listpkt, DATASIZE, 0, (struct sockaddr *)&cliaddr, sizeof(struct sockaddr_in)), "main:sendto");
}

/*
 *  function: setop
 *  ----------------------------
 *  Serve client request
 *
 *  opdata: client packet and client adddress
 *
 *  return: 1 on successfull operation
 *  error: 0
 */
int setop(struct elab opdata) {
    struct pkt synack;
    // TMP for testing 1 packet scenarios
    int listsize = 1;

    sendack(sockd, ACK_POS, opdata.clipacket.seq, listsize, status);

printf("[Server] Waiting for synack...\n"); // TODO in SERVER_TIMEOUT
    recvfrom(sockd, &synack, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len);
printf("[Server] Received [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", synack.op, synack.seq, synack.ack, synack.pktleft, synack.size, synack.data);

    if (opdata.clipacket.op == SYNOP_LIST && synack.op == ACK_POS) {
        managelist();
        return 1;
    }
    if (opdata.clipacket.op == SYNOP_GET && synack.op == ACK_POS) {
printf("File requested %s\n", opdata.clipacket.data);
        // manageget();
    }

printf("Client operation aborted\n");
    return 0;
}

int main(int argc, char const *argv[]) {
    struct pkt cpacket;
    struct elab epacket;
    char *spath = DEFAULT_PATH; // root folder for server
    char *filename, *localpathname;
    struct sembuf oper;

    /* Usage */
    if (argc > 2) {
        fprintf(stderr, "Path from argv[1] set, extra parameters are discarded. [Usage]: %s [<path>]\n", argv[0]);
    }

    /* Init */
    if (argc > 1) spath = (char *)argv[1];
printf("Root folder: %s\n", spath);
    nextseqnum = 1;
    memset((void *)&servaddr, 0, sizeof(struct sockaddr_in));
    sockd = setsock(&servaddr, SERVER_ADDR, SERVER_PORT, SERVER_TIMEOUT, 1);
    len = sizeof(struct sockaddr_in);

    // TMP for testing ack status
    int filesize;

    SemSnd_Wndw=check(semget(IPC_PRIVATE,1,IPC_CREAT|IPC_EXCL|0666),"MAIN: semget global");
    check(semctl(SemSnd_Wndw,0,SETVAL,WSIZE), "MAIN: semctl global");


    while (1) {
        /* Infinite receiving  */
        check_mem(memset(&cliaddr, 0, sizeof(cliaddr)), "main:memset:cliaddr");
        check_mem(memset(&cpacket, 0, sizeof(struct pkt)), "main:memset:cpacket");
printf("[Server] Waiting for synop...\n");
        check(recvfrom(sockd, &cpacket, HEADERSIZE + DATASIZE, 0, (struct sockaddr *)&cliaddr, &len), "main:rcvfrom");
printf("[Server] Received [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", cpacket.op, cpacket.seq, cpacket.ack, cpacket.pktleft, cpacket.size, cpacket.data);

        check_mem(memset(&epacket, 0, sizeof(struct elab)), "main:memset:epacket");
        memcpy(&epacket.cliaddr, &cliaddr, len);
        epacket.clipacket = makepkt(cpacket.op, cpacket.seq, cpacket.ack, cpacket.pktleft, cpacket.size, cpacket.data);
printf("Creating elab [addr:?][port:%d][op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", epacket.cliaddr.sin_port, epacket.clipacket.op, epacket.clipacket.seq, epacket.clipacket.ack, epacket.clipacket.pktleft, epacket.clipacket.size, epacket.clipacket.data);


        /* Operation selection */
        switch (cpacket.op) {

            case SYNOP_LIST: // list
                if (setop(epacket)) {
printf("Operation cmd:%d seq:%d status:completed successfully\n\n", epacket.clipacket.op, epacket.clipacket.seq);
                }else{
printf("Operation cmd:%d seq:%d status:completed unsuccessfully\n\n", epacket.clipacket.op, epacket.clipacket.seq);
                }
                break;

            case SYNOP_GET: // get
                // calculate the size of the arg file
                filename=malloc(cpacket.size*(sizeof(char)));
                printf("filename: %s \n", filename);
                printf("filename: %s \n", cpacket.data);
                printf("lunghezza filename: %d \n", cpacket.size);
                strncpy(filename, cpacket.data, cpacket.size); // salvo il filename del file richiesto
                printf("filename copiato: %s \n", filename);
                localpathname = malloc(DATASIZE * sizeof(char));
        	    sprintf(localpathname, "%s%s", SERVER_FOLDER, filename);
                filesize = calculate_numpkts(localpathname);
                if (filesize == -1) {
                    //sack = makepkt(4, nextseqnum, cpacket.seq, 0, 22, "GET: File non presente");
                    sendack(sockd, ACK_NEG, cpacket.seq, 0, "GET: File non presente");
                }else{
                printf("[SERVER] File selected is %s and it has generate %d pkt to transfer \n", filename, filesize);
                //sack = makepkt(4, nextseqnum, cpacket.seq, filesize, 2, "ok");
                sendack(sockd, ACK_POS, cpacket.seq, filesize, "ok");
                if (get(nextseqnum, filesize, localpathname)) {
                    printf("[SERVER] Sending file %s complete with success \n", filename);
                  }else{
                      printf("[SERVER]Problem with transfer file %s to client  \n", filename);
                      exit(EXIT_FAILURE);
                  }
                }
                printf("My job here is done\n\n");
                free(filename);
                free(localpathname);
                check_mem(memset(&cpacket.data, 0, ((DATASIZE) * sizeof(char))), "main-GET:memset:cpacket");
                break;

            case SYNOP_PUT: // put
            	if (freespacebuf(cpacket.pktleft)) {
            		printf("IL SERVER PUÒ OSPITARE IL FILE %s \n",cpacket.data);
            		sendack(sockd, ACK_POS, cpacket.seq, cpacket.pktleft, "ok");
            		if (put(nextseqnum, cpacket.data, cpacket.pktleft)) {
                    	printf("[SERVER] Receiving file %s complete with success \n", cpacket.data);
                  	} else {
                      printf("[SERVER] Problem in receiving file %s from client \n", cpacket.data);
                      exit(EXIT_FAILURE);
                  }
            	} else {
                	sendack(sockd, ACK_NEG, cpacket.seq, 0, "receive buffer cannot contain filesize");
                	exit(EXIT_FAILURE);
                }
//printf("Operation cmd:%d seq:%d status:completed unsuccessfully\n\n", epacket.clipacket.op, epacket.clipacket.seq);
                break;

            default:
printf("Can't handle this packet\n\n");
                sendack(sockd, ACK_NEG, cpacket.seq, 0, "malformed packet");
                break;
        }
    } // end while

    exit(EXIT_FAILURE);
}
