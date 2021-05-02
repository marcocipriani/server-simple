#include "common.c"
#include "macro.h"

int SemSnd_Wndw;
int sockd; // TODEL sockd->connsd
int connsd;
struct sockaddr_in servaddr;
socklen_t len;
struct sockaddr_in cliaddr; // TODEL cliaddr->getpeername(sockd)
int nextseqnum, initseqserver;
char *msg;
char rcvbuf[45000]; // buffer per la put
void **tstatus;
char *spath = SERVER_FOLDER; // root folder for server

/*
LEGACY
 *  function: waitforack
 *  ----------------------------
 *  Wait for a positive ack
 *
 *  return: 1 on successfully received ack
 *  error: 0

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
 */

int freespacebuf2(int totpkt) {
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

/*
 *  function: check_validity
 *  ----------------------------
 *  Check if an operation requested by the client is valid
 *
 *  return: ACK_POS on success
 *  error: ACK_NEG
 */
int check_validity(char **status, int *pktleft, int op, char *arg){
    char *filename, localpathname[strlen(CLIENT_FOLDER) + (DATASIZE)];

    switch (op) {
        case SYNOP_LIST:
            *status = "ok"; // TMP
            break;

        case SYNOP_GET:
            filename = check_mem(malloc(strlen(arg) * (sizeof(char))), "check_validity:malloc:filename");
            sprintf(localpathname, "%s%s", SERVER_FOLDER, filename);
            if ((*pktleft = calculate_numpkts(localpathname)) == -1){
                *status = "File not available";
printf("Invalid operation on this server\n");
                return ACK_NEG;
            }
            *status = "File available";
            break;

        case SYNOP_PUT:
            *status = "ok"; // TMP
            break;
    }
printf("Valid operation on this server\n");
    return ACK_POS;
}

/*
 *  function: serve_op
 *  ----------------------------
 *  Serve client request
 *
 *  synack: result of the operation establishment
 *  opdata: client packet and client adddress
 *
 *  return: socket id, synack packet
 *  error: -1
 */
int serve_op(struct pkt *synack, struct elab opdata) {
    pid_t me = getpid(); // TODO pthread_self()
    struct pkt ack;
    int opersd;
    int pktleft = 0;
    int status_code;
    char *status; // [DATASIZE]?
    int n;

    /*** Create socket to perform the operation ***/
    opersd = check(setsock(opdata.cliaddr, CLIENT_TIMEOUT, 1), "serve_op:setsock:opersd");

    /*** Create ack ***/
    status_code = check_validity(&status, &pktleft, opdata.clipacket.op, opdata.clipacket.data);
    nextseqnum = arc4random();
    ack = makepkt(status_code, nextseqnum, opdata.clipacket.seq, pktleft, strlen(status), status);

    check(sendto(opersd, &ack, HEADERSIZE + ack.size, 0, (struct sockaddr *)&opdata.cliaddr, sizeof(struct sockaddr_in)), "setop:sendto:ack");
printf("[Server pid:%d sockd:%d] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, opersd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);

    /*** Receive synack (response to ack) from client ***/
printf("[Server pid:%d sockd:%d] Waiting for synack in %d seconds...\n", SERVER_TIMEOUT, me, opersd);
    n = recvfrom(opersd, synack, MAXTRANSUNIT, 0, (struct sockaddr *)&opdata.cliaddr, &len);
printf("[Server pid:%d sockd:%d] Received [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, opersd, synack.op, synack.seq, synack.ack, synack.pktleft, synack.size, synack.data);

    if(n==0){
printf("No synack response from client\n");
        close(opersd);
        return -1;
    }

    if(synack.op == ACK_NEG){
printf("Client operation aborted\n");
        close(opersd);
        return -1;
    }

printf("Client operation continued\n");
    return opersd;
}

//input:puntatore a pila,sem(pkts_to_send),ack_counters,base,nextseqnum,initialseq(not required if pktlft->seq relative),sockd
//timer, estimatedRTT..pid padre
void *thread_sendpkt(void *arg) {
    struct sender_info cargo;//t_info
    struct pkt sndpkt, rcvack;

    //int me;
    int k,n;
    int base;
    union sigval retransmit_info;
    struct sembuf oper;
    cargo = (struct sender_info)&arg;
    // TODO change ptr->aritmetic

    int opersd;// = cargo.//TMPsockd

transmit:
    oper.sem_num = 0;
    oper.sem_op = -1;
    oper.sem_flg = SEM_UNDO;

    check(semop(cargo.semLoc,&oper,1),"THREAD: error wait semLoc");    //wait su semLoc

    oper.sem_num = 0;
    oper.sem_op = -1;
    oper.sem_flg = SEM_UNDO;

    check(semop(cargo.semLoc,&oper,1),"THREAD: error wait global");    //wait su semGlob

    check(pthread_mutex_lock(&cargo.mutex_stack),"THREAD: error lock Stack");      //lock sulla struct stack_elem
    sndpkt = pop_pkt(cargo.stack);
    if (sndpkt==NULL){    //INVALID OPERATION
        check(pthread_mutex_unlock(&cargo.mutex_stack),"THREAD: error unlock Stack");//unlock pila

        oper.sem_num = 0;
        oper.sem_op = 1;
        oper.sem_flg = SEM_UNDO;

        check(semop(SemSnd_Wndw,&oper,1),"THREAD: error signal global");    //signal a semGlobal

        //serve la wait a semLocale??
        pthread_exit(NULL);
    }
    oper.sem_num = 0;                                                 //se pop_pkt a buon fine
    oper.sem_op = 1;                                                  //signal a semGlobal
    oper.sem_flg = SEM_UNDO;

    check(semop(SemSnd_Wndw,&oper,1),"THREAD: error signal global");

    check(pthread_mutex_unlock(&cargo.mutex_stack),"THREAD: error unlock Stack");   //unlock struct stack_elem

    sendto(opersd, &sndpkt, HEADERSIZE + sndpkt.size, 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr)); // TMP cliaddr parsed from sender_info
    //nextseqnum++;//LA RITRASMISSIONE NON DEVE ALZARLO
    //---->lock timer (try lock)
    if (cargo.timer == 0){       //avviso il padre di dormire per timeout_Interval
      //----->lock timer
      *(cargo.timer)=1;  //corretto scritto cosi?
      //avvia sampleRTT
      oper.sem_num = 0;
      oper.sem_op = 1;                                                  //signal a semTimer
      oper.sem_flg = SEM_UNDO;

      check(semop(cargo.semTimer,&oper,1),"THREAD: error signal semTimer");
      //unlock timer
    }
check_ack:
    n=recvfrom(opersd, &rcvack, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len); // TMP cliaddr parsed from sender_info

    check(pthread_mutex_lock(&cargo->mutex_ack_counter),"THREAD: error lock Ack Counters");

printf("sono il thread # %d e' ho ricevuto l'ack del pkt #%d \n", me, (rcvack.ack) - (cargo->initialseq) + 1);
printf("valore di partenza in counter[%d] : %d \n", (rcvack.ack) - (cargo->initialseq), cargo->ack_counters[(rcvack.ack) - (cargo->initialseq)]);

    if(n>0){
      if(rcvack.ack-*(cargo->base)>WSIZE || rcvack.ack<base-1){    //ack fuori finestra
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
            oper.sem_flg = SEM_UNDO;

            check(semop(SemSnd_Wndw,&oper,1),"THREAD: error signal global at received ack ");
  printf("valore aggiornato in counter[%d] : %d \n", k - (cargo->initialseq), cargo->ack_counters[k - (cargo->initialseq)]);
          }
          check(pthread_mutex_unlock(&cargo->mutex_ack_counter),"THREAD: error unlock Ack Counters");
          if(rcvack.ack == cargo->initialseq+cargo->numpkt){//if(rcvack.pktlft==0)
  printf("GET:Ho riscontrato l'ultimo ack del file\n");
              kill(cargo->father_pid,SIGLASTACK);  //finito il file-il padre dovrà mandare un ack
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
              // sigqueue(cargo->father_pid, SIGRETRANSMIT, retransmit_info);
              // signature is sigqueue(pid_t pid, int sig, const union sigval value);
          }
          else {
              (cargo->ack_counters[(rcvack.ack) - (cargo->initialseq)])=(int)(cargo->ack_counters[(rcvack.ack) - (cargo->initialseq)])+1;
              check(pthread_mutex_unlock(&cargo->mutex_ack_counter),"THREAD: error unlock Ack Counters");
              goto check_ack;
          }
      }
    }
    //se non ho ricevuto niente da rcvfrom
    if(cargo->ack_counters[sndpkt.seq-sender_info->initialseq/*pktleft if setted to seq_relative*/]>0){ //se il pkt che ho inviato è stato ackato trasmetto uno nuovo
      check(pthread_mutex_unlock(&cargo->mutex_ack_counter),"THREAD: error unlock Ack Counters");
      goto transmit;
    }
    else{       //se il mio pkt non è stato ackato continuo ad aspettare l'ack
      check(pthread_mutex_unlock(&cargo->mutex_ack_counter),"THREAD: error unlock Ack Counters");
      goto check_ack;
    }
}

/*
 *  function: get
 *  ----------------------------
 *  Send file to the client
 *
 *  arg: synop packet from client, client address
 *
 *  return: -
 *  error: -
 */
// OLD: int iseq, int numpkt, char *filename
void get(void *arg) { // iseq=11,iack=31,numpkt=10,filename="pluto.jpg"
    struct pkt *sendpkt;
    int fd;
    int i, j, k, z;
    pthread_t *tid;
    int *counter;
    int aux,oldBase;
    char *dati;
    pthread_mutex_t mtxStack,mtxAck_counter;
    int semTimer,semPkt_to_send;
    struct sender_info t_info;
    pid_t pid;
    int init = initseqserver;

    struct elab synop = (struct elab)&arg;
    struct pkt synack;
    int opersd;

    opersd = serve_op(&synack, synop);
    if(opersd < 0){
printf("Operation op:%d seq:%d unsuccessful\n", synop.op, synop.seq);
        pthread_exit(NULL);
    }

    // parse these from synack: int iseq, int numpkt, char *filename
    char *filename;
    int numpkt=synack.pktleft;
    int iseq=synac.ack + 1;
    int base = init;
    int timer;
    double estimatedRTT, timeout_Interval;
    struct sample startRTT;
    struct sembuf oper;
    struct timespec start;

    // TODEL
    if (strcmp(synack.data, "ok") == 0) { /* ho ricevuto ack positivo dal client */
printf("[SERVER] Connection established \n");

        startRTT.start=&start;
        startRTT.seq=-1;

        strncpy(filename,opdata.clipacket.data,opdata.clipacket.size);
        localpathname = malloc(DATASIZE * sizeof(char));
        sprintf(localpathname, "%s%s", SERVER_FOLDER, filename);

        pid = getpid();
        pktstack stackPtr = NULL;
        *timeout_Interval=TIMEINTERVAL;

        fd = open(localpathname, O_RDONLY, 00700); // apertura file da inviare
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
              push_pkt(&stackPtr, sendpkt[z]);
            }
            /*****INIZIALIZZAZIONE SEMAFORI E MUTEX**********/
            check(pthread_mutex_init(&mtxStack,NULL),"GET: errore pthread_mutex_init struct stack_elem");

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
            t_info.sockid = opersd; //per ora
            t_info.timer = &timer;
            t_info.estimatedRTT = &estimatedRTT;
            t_info.startRTT = &startRTT;
            t_info.timeout_Interval = &timeout_Interval;
            t_info.father_pid = pid;



            for(j=0;j<WSIZE;j++)
            if(pthread_create(&tid[j], NULL, thread_sendpkt, (void *)t_info) != 0){
printf("server:ERRORE pthread_create GET in main");
                exit(EXIT_FAILURE);
            }


          signal(SIGRETRANSMIT,push_base);//usa sigaction
          //signal(/*stop timer-base aggiornata*/);
          while((base-initialseq)<=numpkt){
            oper.sem_num = 0;
            oper.sem_op = -1;
            oper.sem_flg = SEM_UNDO;

            check(semop(semTimer,&oper,1),"THREAD: error wait semTimer");   //WAIT su semTimer

            int oldBase=base;
            usleep(*timeout_Interval);
            if (counter[oldBase - initialseq]==0){ //if (control==base)   //RITRASMISSIONE
              push_pkt(&stackPtr,send[oldBase - initialseq])  //o handler()signal(sem_ppkts_to_send)

              oper.sem_num = 0;                                                 //se RITRASMISSIONE
              oper.sem_op = 1;                                                  //signal a semPkt_to_send
              oper.sem_flg = SEM_UNDO;

              check(semop(semPkt_to_send,&oper,1),"GET: error signal semLocal ");

              //lock timer
              *timer=0;
              //unlock timer
            }
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

    int opersd; // TMP returned from setop

    npkt = pktleft;
    edgepkt = npkt;
    check(recvfrom(opersd, &rack, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len), "PUT-server:recvfrom ack-client");
printf("[Server] Received ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", rack.op, rack.seq, rack.ack, rack.pktleft, rack.size, rack.data);

    // strcmp(rack.op, ACK_POS)
    if (strcmp(rack.data, "ok") == 0) {
    	initseqserver = rack.seq;
    	localpathname = malloc(DATASIZE * sizeof(char));
    	sprintf(localpathname, "%s%s", SERVER_FOLDER, pathname);

receiver:
        while(npkt>0){
            check(recvfrom(opersd,&cargo, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len), "PUT-server:recvfrom Cargo");
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
                sendack2(opersd, ACK_POS, cargo.seq, cargo.pktleft, "ok");
printf("[Server] il pacchetto #%d e' stato scritto in pos:%d del buffer\n",cargo.seq,pos);
            }else{
            	printf("[Server] pacchetto già ricevuto, posso scartarlo \n");
                sendack2(opersd, ACK_POS, cargo.seq, cargo.pktleft, "ok");
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
 *  function: list
 *  ----------------------------
 *  Execute makelist function and send the result to the client
 *
 *  return: -
 *  error: -
 */
void list() {
    char *res = malloc(((DATASIZE)-1)*sizeof(char)); // client has to put \0 at the end
    char **resptr = &res;
    struct pkt listpkt;
    int opersd; // TMP returned from setop

    makelist(resptr, spath);
    listpkt = makepkt(CARGO, nextseqnum, 1, 1, strlen(res), res);
printf("[Server] Sending list [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", listpkt.op, listpkt.seq, listpkt.ack, listpkt.pktleft, listpkt.size, (char *)listpkt.data);
    check(sendto(opersd, &listpkt, DATASIZE, 0, (struct sockaddr *)&cliaddr, sizeof(struct sockaddr_in)), "main:sendto");
}

int main(int argc, char const *argv[]) {
    struct pkt synop, ack; // ack only when rejecting packets with bad op code
    struct sockaddr_in cliaddr;
    struct elab opdata;
    pid_t me;
    pthread_t tid;
    int ongoing_operations;
    char *spath = DEFAULT_PATH; // root folder for server

    char *filename, *localpathname; // TMP?
    struct sembuf oper; // TMP?

    /*** Usage ***/
    if (argc > 2) {
        fprintf(stderr, "Path from argv[1] set, extra parameters are discarded. [Usage]: %s [<path>]\n", argv[0]);
    }

    /*** Init ***/
    if (argc > 1) spath = (char *)argv[1];
    me = getpid();
printf("Root folder: %s\n", spath);
    memset((void *)&servaddr, 0, sizeof(struct sockaddr_in));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    connsd = setsock(servaddr, 0, 1);
    len = sizeof(struct sockaddr_in);
    ongoing_operations = 0;

    SemSnd_Wndw=check(semget(IPC_PRIVATE,1,IPC_CREAT|IPC_EXCL|0666),"MAIN: semget global");
    check(semctl(SemSnd_Wndw,0,SETVAL,WSIZE), "MAIN: semctl global");

   /*** Receiving synop (max BACKLOG) ***/
    while (1) {
        // Reset address and packet of the last operation
        check_mem(memset(&cliaddr, 0, sizeof(struct sockaddr_in)), "main:memset:cliaddr");
        check_mem(memset(&synop, 0, sizeof(struct pkt)), "main:memset:synop");

printf("[Server pid:%d sockd:%d] Waiting for synop...\n", me, connsd);
        check(recvfrom(connsd, &synop, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len), "main:rcvfrom:synop");
printf("[Server pid:%d sockd:%d] Received [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, connsd, synop.op, synop.seq, synop.ack, synop.pktleft, synop.size, synop.data);

        // TODO if ongoing_operations >= BACKLOG send negative ack and goto recvfrom synop

        // Prepare op for child
        check_mem(memset(&opdata, 0, sizeof(struct elab)), "main:memset:opdata");
        memcpy(&opdata.cliaddr, &cliaddr, len);
        opdata.clipacket = makepkt(synop.op, synop.seq, synop.ack, synop.pktleft, synop.size, synop.data);
printf("Creating elab [addr:%d][port:%d][op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", opdata.cliaddr.sin_addr.s_addr, opdata.cliaddr.sin_port, opdata.clipacket.op, opdata.clipacket.seq, opdata.clipacket.ack, opdata.clipacket.pktleft, opdata.clipacket.size, opdata.clipacket.data);

        /*** Operation selection ***/
        switch (opdata.clipacket.op) {

            case SYNOP_LIST:
                pthread_create(&tid, NULL, list, (void *)&opdata);
                ++ongoing_operations;
printf("Passed elab to child %d\n\n", tid);
                break;

            case SYNOP_GET:
                /*if (!(chec_validate(SYNOP_GET))){
                check_mem(memset(&ack, 0, sizeof(struct pkt)), "main:get:ack");
                ack = makepkt(ACK_NEG, nextseqnum, opdata.clipacket.seq, opdata.clipacket.pktleft, strlen("malformed packet"), "malformed packet");
                check(sendto(connsd, &ack, HEADERSIZE + ack.size, 0, (struct sockaddr *)&opdata.cliaddr, sizeof(struct sockaddr_in)), "main:sendto:ack:malformed_packet");
printf("server: Operation denied \n")
                }*/
                pthread_create(&tid, NULL, get, (void *)&opdata);
                ++ongoing_operations;
printf("Passed elab to child %d\n\n", tid);
                /* LEGACY
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
                */
                break;

            case SYNOP_PUT:
                pthread_create(&tid, NULL, put, (void *)&opdata);
                ++ongoing_operations;
printf("Passed elab to child %d\n\n", tid);
                /* LEGACY
            	if (freespacebuf2(cpacket.pktleft)) {
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
                */
                break;

            default:
printf("Can't handle this packet\n\n");
                // polite server: send ack with negative status instead of ignoring
                check_mem(memset(&ack, 0, sizeof(struct pkt)), "main:memset:ack");
                ack = makepkt(ACK_NEG, nextseqnum, opdata.clipacket.seq, opdata.clipacket.pktleft, strlen("malformed packet"), "malformed packet");
                check(sendto(connsd, &ack, HEADERSIZE + ack.size, 0, (struct sockaddr *)&opdata.cliaddr, sizeof(struct sockaddr_in)), "main:sendto:ack:malformed_packet");
printf("[Server pid:%d sockd:%d] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, connsd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
                break;
        }
    } // end while

    exit(EXIT_FAILURE);
}
