#include "common.c"
#include "macro.h"

int SemSnd_Wndw;
int sockd; // TODEL sockd->connsd
int connsd;
struct sockaddr_in listen_addr;
socklen_t len;
pthread_mutex_t mtxlist;
char *msg;
void **tstatus;
char *spath = SERVER_FOLDER; // root folder for server
pthread_t *ttid; // TODO sigqueue from writer to father

char rcvbuf[SERVER_RCVBUFSIZE*(DATASIZE)];
index_stack free_pages_rcvbuf;
pthread_mutex_t mutex_rcvbuf;
pthread_t server_ttid[SERVER_NUMTHREADS + 2]; // TMP not to do in global, pass it to exit_handler

/*
 *  function: check_validity
 *  ----------------------------
 *  Check if an operation requested by the client is valid
 *
 *  status: string which denotes the validity of the operation
 *  pktleft: quantity of packets the arg file is made of
 *  op: operation macro
 *  arg: filename
 *
 *  return: ACK_POS on success, status string on status param, size of file in packets on pktleft param
 *  error: ACK_NEG
 */
int check_validity(char **status, int *pktleft, int op, char *arg){
    char localpathname[strlen(SERVER_FOLDER)+(DATASIZE)];

    switch (op) {
        case SYNOP_LIST:
            *status = "ok"; // TMP
            break;

        case SYNOP_GET:
            sprintf(localpathname, "%s%s", SERVER_FOLDER, arg);
            if ((*pktleft = calculate_numpkts(localpathname)) == -1){
                *status = FILE_NOT_AVAILABLE;
printf("check_validity: Invalid operation on this server\n\n");
                return ACK_NEG;
            }
            *status = FILE_AVAILABLE;
            break;

        case SYNOP_PUT:
            *status = "ok"; // TMP
            break;
    }

    check_mem(memset(localpathname, 0, strlen(SERVER_FOLDER)+(DATASIZE)), "check_validity:memset:localpathname");
printf("check_validity: Valid operation on this server\n\n");
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
int serve_op(struct pkt *synack, struct elab opdata){
    int me = (int)pthread_self();
    struct pkt ack;
    int opersd;
    int pktleft = 0;
    int status_code;
    int initseq;
    char *status; // [DATASIZE]?
    int n;

    /*** Create socket to perform the operation ***/
    opersd = check(setsock(opdata.cliaddr, SERVER_TIMEOUT), "serve_op:setsock:opersd");
    check(connect(opersd, (struct sockaddr *)&opdata.cliaddr, len), "serve_op:connect:cliaddr");

    /*** Create ack ***/
    status_code = check_validity(&status, &pktleft, opdata.clipacket.op, opdata.clipacket.data);
    //initseq = arc4random_uniform(MAXSEQNUM);
    //srand((unsigned int)time(1));
    initseq=rand()%100;

    ack = makepkt(status_code, initseq, opdata.clipacket.seq, pktleft, strlen(status), status);

    //check(sendto(opersd, &ack, HEADERSIZE + ack.size, 0, (struct sockaddr *)&opdata.cliaddr, len), "setop:sendto:ack");
    /*if (simulateloss(1))*/  check(send(opersd, &ack, HEADERSIZE + ack.size, 0), "setop:send:ack");
printf("[Server:serve_op tid:%d sockd:%d] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n\n", me, opersd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);

    /*** Receive synack (response to ack) from client ***/
    printf("\tWaiting for synack in %d seconds...\n", SERVER_TIMEOUT);
    memset(synack, 0, sizeof(struct pkt));
    n = recv(opersd, synack, MAXPKTSIZE, 0);

    if(n<1){
        printf("\tNo synack response from client\n");
        close(opersd);
        return -1;
    }

printf("[Server:serve_op tid:%d sockd:%d] Received synack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n\n", me, opersd, synack->op, synack->seq, synack->ack, synack->pktleft, synack->size, (char*)synack->data);

    if(synack->op == ACK_NEG){
        printf("\tClient operation aborted\n");
        close(opersd);
        return -1;
    }

    return opersd;
}

/*
 *  function: kill_handler
 *  ----------------------------
 *  Terminate operation
 *
 *  return: -
 *  error: -
 */
void kill_handler(){
    for(int i=0;i<SERVER_SWND_SIZE;i++){
        pthread_cancel(ttid[i]);
    }
printf("Server operation completed\n\n");
    pthread_exit(NULL);
}

/*
 *  function: server_kill_handler
 *  ----------------------------
 *  Complete operation terminating every thread involved
 *
 *  return: -
 *  error: -
 */
void server_kill_handler(){
    //struct receiver_info info = *((struct receiver_info *)arg); // for ttid

    for(int i=0;i<CLIENT_NUMTHREADS;i++){
        pthread_cancel(ttid[i]);
    }
printf("Server operation completed\n\n");
    pthread_exit(NULL);
};

/*
 *  function: thread_sendpkt
 *  ----------------------------
 *  Send packet to client and wait for at least an any ack
 *
 *  arg: information about transfer from get
 *
 *  return: -
 *  error: -
 */
void thread_sendpkt(void *arg){
    int me = (int)pthread_self();
    struct sender_info cargo;//t_info
    struct pkt sndpkt, rcvack;
    int k,n;
    int base; // unused
    struct sembuf oper, signal_retransmit;
    cargo = *((struct sender_info *)arg);
    struct timespec end;

    int opersd = cargo.sockd;
    socklen_t len;

transmit:
    memset(&sndpkt,0,sizeof(struct pkt));
    oper.sem_num = 0;
    oper.sem_op = -1;
    oper.sem_flg = SEM_UNDO;

    check(semop(cargo.sem_readypkts,&oper,1),"THREAD: error wait sem_readypkts");    //wait su semLocale=pkts_to_send
                                                                      //controllo che ci siano pkt da inviare

    oper.sem_num = 0;
    oper.sem_op = -1;
    oper.sem_flg = SEM_UNDO;

    check(semop(SemSnd_Wndw,&oper,1),"THREAD: error wait global");    //wait su semGlob

    check(pthread_mutex_lock(&cargo.mutex_stack),"THREAD: error lock Stack");      //lock sulla struct stack_elem
printf("ho preso il lock\n");
    int res=pop_pkt(cargo.stack,&sndpkt);

printf("ho fatto una pop %d \n",res);
/*
    oper.sem_num = 0;                                                 //se pop_pkt a buon fine
    oper.sem_op = 1;                                                  //signal a semGlobal
    oper.sem_flg = SEM_UNDO;

    check(semop(SemSnd_Wndw,&oper,1),"THREAD: error signal global");
*/
    pthread_mutex_unlock(&cargo.mutex_stack);   //unlock struct stack_elem
printf("ho rilasciato il lock\n");
    //sendto(opersd, &sndpkt, HEADERSIZE + sndpkt.size, 0, (struct sockaddr *)&cliaddr, sizeof(struct sockaddr_in));
    if (simulateloss(0)) check(send(cargo.sockd, &sndpkt, HEADERSIZE + sndpkt.size, 0), "thread_sendpkt:send:cargo");
printf("[Server tid:%d sockd:%d] Sended packet [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d]\n\n", me, opersd, sndpkt.op, sndpkt.seq, sndpkt.ack, sndpkt.pktleft, sndpkt.size);

    if(*(cargo.startRTT.seq)==-1){
        clock_gettime( CLOCK_REALTIME,cargo.startRTT.start);
printf("cargo.startRTT->start; %d  %lf per pkt; %d\n",(int)cargo.startRTT.start->tv_sec,(float)(1e-9)*cargo.startRTT.start->tv_nsec,sndpkt.seq-cargo.initialseq);
        *(cargo.startRTT.seq)=sndpkt.seq;
    }
/*    pthread_mutex_trylock(&cargo.mutex_time);
    if (*(cargo.timer) == 0){       //avviso il padre di dormire per timeout_Interval
      //----->lock timer
      *(cargo.timer)=1;  //corretto scritto cosi?
      //avvia sampleRTT
      oper.sem_num = 0;
      oper.sem_op = 1;                                                  //signal a semTimer
      oper.sem_flg = SEM_UNDO;

      check(semop(cargo.semTimer,&oper,1),"THREAD: error signal semTimer");
    }
    check(pthread_mutex_unlock(&cargo.mutex_time),"THREAD: error unlock time");*/
check_ack:
    n = recv(opersd, &rcvack, MAXPKTSIZE, 0);

    if(pthread_mutex_lock(&cargo.mutex_ack_counter) != 0){
        fprintf(stderr, "thread_sendpkt:pthread_mutex_lock:mutex_ack_counter\n");
        exit(EXIT_FAILURE);
    }

    if(n>0){
printf("sono il thread # %d e' ho ricevuto l'ack del pkt #%d \n", me, (rcvack.ack) - (cargo.initialseq) + 1);
printf("valore di partenza in counter[%d] : %d \n", (rcvack.ack) - (cargo.initialseq), cargo.ack_counters[(rcvack.ack) - (cargo.initialseq)]);

        if(*(cargo.startRTT.seq)==rcvack.ack){
            *(cargo.startRTT.seq)=-1;
            clock_gettime( CLOCK_REALTIME,&end);
printf("end %d  %lf per pkt:%d\n",(int)end.tv_sec,(float)(1e-9)*end.tv_nsec,rcvack.ack-cargo.initialseq);
            int sampleRTT = ((end.tv_sec - cargo.startRTT.start->tv_sec) + (1e-9)*(end.tv_nsec - cargo.startRTT.start->tv_nsec))*(1e6);
printf("SampleRTT: %d ns\n",sampleRTT);
            *(cargo.estimatedRTT)=(0.875*(*cargo.estimatedRTT))+(0.125*sampleRTT);
printf("new estimatedRTT: %d ns\n",*(cargo.estimatedRTT));
            *(cargo.devRTT)=(0.75*(*cargo.devRTT))+(0.25*(abs(sampleRTT-(*cargo.estimatedRTT))));
printf("new devRTT: %d ns\n",*(cargo.devRTT));
            *(cargo.timeout_Interval)=*(cargo.estimatedRTT)+4*(*cargo.devRTT);
printf("new timeout_Interval: %d ns\n",*(cargo.timeout_Interval));
        }

        if(rcvack.ack-(*(cargo.base))>SERVER_SWND_SIZE || rcvack.ack<(*(cargo.base)-1)){    //ack fuori finestra
            check(pthread_mutex_unlock(&cargo.mutex_ack_counter),"THREAD: error unlock Ack Counters");
            goto check_ack;
        }

        if(rcvack.ack >= *(cargo.base)){   //ricevo un ack nella finestra
            for (k=*(cargo.base);k<=rcvack.ack;k++){
                //se pktlft=seq relative..da fare
                cargo.ack_counters[k - (cargo.initialseq)] = (int)cargo.ack_counters[k - (cargo.initialseq)] + 1; //sottraggo il num.seq iniziale
                (*(cargo.base))++; //da controllare

                oper.sem_num = 0;                                                 //se ack in finestra
                oper.sem_op = 1;                                                  //signal a semGlobal
                oper.sem_flg = SEM_UNDO;
                check(semop(SemSnd_Wndw,&oper,1),"THREAD: error signal global at received ack ");
  printf("valore aggiornato in counter[%d] : %d \n", k - (cargo.initialseq), cargo.ack_counters[k - (cargo.initialseq)]);
            }

            check(pthread_mutex_unlock(&cargo.mutex_ack_counter),"THREAD: error unlock Ack Counters");
            if(rcvack.ack+1 == cargo.initialseq+cargo.numpkts){
                printf("\t(Server:get tid:%d) Received last ack for file\n", me);
                pthread_kill(cargo.father_pid, SIGFINAL);
                pthread_exit(NULL);
            }
            goto transmit;
        } else if(rcvack.ack==(*cargo.base)-1){   //ack duplicato
            if ((cargo.ack_counters[(rcvack.ack) - (cargo.initialseq)]) == 3) { // 3 duplicated acks
printf("dovrei fare una fast retransmit del pkt con #seg: %d/n", rcvack.ack);
                (cargo.ack_counters[(rcvack.ack) - (cargo.initialseq)]) = 1;//(cargo.p[(rcvack.ack) - (cargo.initialseq)]) + 1;
printf("azzero il counter[%d] : %d \n", (rcvack.ack) - (cargo.initialseq), cargo.ack_counters[(rcvack.ack) - (cargo.initialseq)]);

                check(pthread_mutex_unlock(&cargo.mutex_ack_counter),"THREAD: error unlock Ack Counters");

                if(pthread_mutex_lock(&cargo.mutex_stack) != 0){
                    fprintf(stderr, "thread_sendpkt:pthread_mutex_lock:mutex_stack\n");
                    exit(EXIT_FAILURE);
                }

                check(push_pkt(cargo.stack, sndpkt), "thread_sendpkt:pop_pkt:stack");
printf("(Server:thread_sendpkt tid%d) Locked the stack to put pkt after retransmit and pushed the packet seq:%d back into the stack\n\n", me, sndpkt.seq);

                if(pthread_mutex_unlock(&cargo.mutex_stack) != 0){
                    fprintf(stderr, "thread_sendpkt:pthread_mutex_unlock:mutex_stack\n");
                    exit(EXIT_FAILURE);
                }

                // poking the next thread waiting on transmit
                signal_retransmit.sem_num = 0;
                signal_retransmit.sem_op = 1;
                signal_retransmit.sem_flg = SEM_UNDO;
                check(semop(cargo.sem_readypkts, &oper, 1),"thread_sendpkt:semop:signal:sem_readypkts");
                check(semop(SemSnd_Wndw, &oper, 1),"thread_sendpkt:semop:signal:sem_readypkts");
            } else {
                (cargo.ack_counters[(rcvack.ack) - (cargo.initialseq)])=(int)(cargo.ack_counters[(rcvack.ack) - (cargo.initialseq)])+1;
                check(pthread_mutex_unlock(&cargo.mutex_ack_counter),"THREAD: error unlock Ack Counters");
                goto check_ack;
            }
        }
    } // end if(n>0)

    //se non ho ricevuto niente da rcvfrom
    if(cargo.ack_counters[sndpkt.seq-cargo.initialseq]>0){ //se il pkt che ho inviato è stato ackato trasmetto uno nuovo
        check(pthread_mutex_unlock(&cargo.mutex_ack_counter),"THREAD: error unlock Ack Counters");
        goto transmit;
    }else{ //se il mio pkt non è stato ackato continuo ad aspettare l'ack
        check(pthread_mutex_unlock(&cargo.mutex_ack_counter),"THREAD: error unlock Ack Counters");
        goto check_ack;
    }
}

/*
 *  function: writer
 *  ----------------------------
 *  Write packets to the file
 *
 *  arg: information about transfer from put
 *
 *  return: -
 *  error: -
 */
void writer(void *arg){
    int me = (int)pthread_self();
    int last_write_made = 0; // from 0 to numpkts
    struct sembuf wait_writebase;
    int n_bytes_to_write = (DATASIZE);
    int fd;
    char *localpathname = check_mem(malloc(DATASIZE+strlen(CLIENT_FOLDER)*sizeof(char)), "writer:malloc:localpathname");
    struct receiver_info info = *((struct receiver_info *)arg);
    int free_index;

    sprintf(localpathname, "%s%s", CLIENT_FOLDER, info.filename);
    fd = open(localpathname, O_RDWR|O_CREAT|O_TRUNC, 0666);
printf("(Server:writer tid:%d) Opened localpathname:%s\n\n", me, localpathname);

    while(last_write_made < info.numpkts){
        wait_writebase.sem_num = 0;
        wait_writebase.sem_op = -1;
        wait_writebase.sem_flg = SEM_UNDO;
        check(semop(info.sem_writebase, &wait_writebase, 1), "receiver:semop:sem_readypkts");

        pthread_mutex_lock(&info.mutex_rcvbuf);

        while(info.file_cells[last_write_made] != -1){
            free_index = info.file_cells[last_write_made];
            if(last_write_made == info.numpkts-1)
                n_bytes_to_write = (*info.last_packet_size);
            write(fd, &rcvbuf[free_index*(DATASIZE)], n_bytes_to_write);

            check_mem(memset(&rcvbuf[free_index*(DATASIZE)], 0, DATASIZE), "receiver:memset:rcvbuf[free_index]");
            check(push_index(&free_pages_rcvbuf, free_index), "receiver:push_index");
            info.file_cells[last_write_made]=-1;
            last_write_made = last_write_made +1;
printf("(Server:writer tid:%d) Written %d bytes from rcvbuf[%d] to %s\n\n", me, n_bytes_to_write, free_index, localpathname);
            if(last_write_made == info.numpkts) break;
        }

        pthread_mutex_unlock(&info.mutex_rcvbuf);
    }

    close(fd);
    pthread_kill(ttid[CLIENT_NUMTHREADS + 1], SIGFINAL);
    pthread_exit(NULL);
}

/*
 *  function: receiver
 *  ----------------------------
 *  Process received packets and send acks
 *
 *  arg: information about transfer from put
 *
 *  return: -
 *  error: -
 */
void receiver(void *arg){
    int me = (int)pthread_self();
    struct sembuf wait_readypkts;
    struct sembuf signal_writebase;
    struct pkt cargo, ack;
    int i;
    struct receiver_info info = *((struct receiver_info *)arg);

waitforpkt:
    // wait if there are packets to be read
    wait_readypkts.sem_num = 0;
    wait_readypkts.sem_op = -1;
    wait_readypkts.sem_flg = SEM_UNDO;
    check(semop(info.sem_readypkts, &wait_readypkts, 1), "receiver:semop:sem_readypkts");

    pthread_mutex_lock(&info.mutex_rcvqueue);
    if(dequeue(info.received_pkts, &cargo) == -1){
        fprintf(stderr, "Can't dequeue packet from received_pkts\n");
        // TODO fatal exit?
    }
    //pthread_mutex_unlock(&info.mutex_rcvqueue); TODEL if mutex_rcvbuf = mutex_rcvqueue
    usleep(1000); // TMP
    pthread_mutex_lock(&info.mutex_rcvbuf);

    if(info.file_cells[cargo.seq - info.init_transfer_seq] == -1){ // packet still not processed
        i = check(pop_index(&free_pages_rcvbuf), "receiver:pop_index:free_pages_rcvbuf");
        check_mem(memcpy(&rcvbuf[i*(DATASIZE)], &cargo.data, cargo.size), "receiver:memcpy:cargo");
        info.file_cells[cargo.seq-info.init_transfer_seq] = i;
printf("(Server:receiver tid:%d) Dequeued %d packet and stored it in rcvbuf[%d]\n\n", me, cargo.seq-info.init_transfer_seq, i);

        if(cargo.seq == *info.rcvbase){
            (*info.nextseqnum)++; // TODO still necessary
            while(info.file_cells[(*info.rcvbase)-info.init_transfer_seq] != -1){
                (*info.rcvbase)++; // increase rcvbase for every packet already processed
                if((*info.rcvbase)-info.init_transfer_seq == info.numpkts) break;
            }
            ack = makepkt(ACK_POS, *info.nextseqnum, (*info.rcvbase)-1, cargo.pktleft, strlen(CARGO_OK), CARGO_OK);
printf("[Server:receiver tid:%d sockd:%d] Sending ack-newbase [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n\n", me, info.sockd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
            check(send(info.sockd, &ack, HEADERSIZE + ack.size, 0) , "receiver:send:ack-newbase");

            // tell the thread doing writer to write base cargo packet
            signal_writebase.sem_num = 0;
            signal_writebase.sem_op = 1;
            signal_writebase.sem_flg = SEM_UNDO;
            check(semop(info.sem_writebase, &signal_writebase, 1), "receiver:semop:signal:sem_writebase");
        }else{
            (*info.nextseqnum)++; // TODO still necessary
            ack = makepkt(ACK_POS, *info.nextseqnum, (*info.rcvbase)-1, cargo.pktleft, strlen(CARGO_MISSING), CARGO_MISSING);
printf("[Server:receiver tid:%d sockd:%d] Sending ack-missingcargo [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n\n", me, info.sockd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
            check(send(info.sockd, &ack, HEADERSIZE + ack.size, 0) , "receiver:send:ack-missingcargo");
        }
    }else{
        ack = makepkt(ACK_POS, *info.nextseqnum, (*info.rcvbase)-1, cargo.pktleft, strlen(CARGO_MISSING), CARGO_MISSING);
printf("[Server:receiver tid:%d sockd:%d] Sending ack-missingcargo [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n\n", me, info.sockd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
        check(send(info.sockd, &ack, HEADERSIZE + ack.size, 0) , "receiver:send:ack-missingcargo");
    }

    pthread_mutex_unlock(&info.mutex_rcvbuf);
    pthread_mutex_unlock(&info.mutex_rcvqueue);

    check_mem(memset((void *)&cargo, 0, HEADERSIZE + cargo.size), "receiver:memset:cargo");
    check_mem(memset((void *)&ack, 0, HEADERSIZE + ack.size), "receiver:memset:ack");
    goto waitforpkt;
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
void get(void *arg){
    int me = (int)pthread_self();
    struct pkt *sendpkt;
    int fd;
    int i, j, k, z;
    pthread_t tid;
    int *counter;
    int aux, oldBase;
    char *filedata;
    pthread_mutex_t mtxTime,mtxStack,mtxAck_counter;
    int semTimer,semPkt_to_send;
    struct sender_info t_info;
    struct sigaction act_lastack;
    struct elab synop = *((struct elab*)arg);
    struct pkt synack;
    int opersd;
    char filename[128], *localpathname;
    struct sembuf oper;
    struct timespec start;
    int timer;
    int rtt= -1;
    int estimatedRTT, timeout_Interval,devRTT;
    struct sample startRTT;

    opersd = serve_op(&synack, synop);
    if(opersd < 0){
printf("(Server:get tid:%d) Operation op:%d seq:%d unsuccessful, exiting operation\n\n", me, synop.clipacket.op, synop.clipacket.seq);
        pthread_exit(NULL);
    }
printf("(Server:get tid:%d) Handshake successful, continuing operation\n\n", me);

    int numpkts = synack.pktleft;
    int iseq = synack.ack + 1;
    int base = synack.ack + 1;
    int init = synack.ack + 1;

    startRTT.start=&start;
    startRTT.seq=&rtt;
    estimatedRTT=2000;
    devRTT=500;
    //filename=(char *)malloc((synop.clipacket.size+1)*(sizeof(char)));
    //strncpy(filename,synop.clipacket.data,(size_t)synop.clipacket.size);
    localpathname = (char *)malloc((DATASIZE) * sizeof(char));
    sprintf(localpathname, "%s%s", SERVER_FOLDER, synop.clipacket.data);
    char cwd[DATASIZE];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
       printf("Current working dir: %s\n", cwd);
    }else{
        perror("getcwd() error");
    }
    tid = pthread_self();
    pktstack stackPtr = NULL;
    timeout_Interval=TIMEINTERVAL;
printf("clipacket: %s e size; %d\n",synop.clipacket.data,synop.clipacket.size);
printf("filename: %s\n",filename);
printf("localpathname: %s\n",localpathname);
    fd = check(open(localpathname, O_RDWR, 0666), "get:open:fd");
printf("filename: %s\n",filename);
printf("localpathname: %s\n",localpathname);
    //check_mem(malloc(filename,0,128)
    //free(filename);
    //free(localpathname);


printf("Thread %d: inizio trasferimento \n", me);
    sendpkt = malloc((numpkts) * sizeof(struct pkt)); /*Alloca la memoria per thread che eseguiranno la get */
    check_mem(sendpkt, "get:malloc:sendpkt");

    counter = malloc(numpkts*sizeof(int));
    check_mem(counter, "get:malloc:counter");
    for(z=0; z<numpkts; z++){
        counter[z] = 0; // inizializza a 0 il counter
    }

    ttid = malloc(SERVER_SWND_SIZE*sizeof(pthread_t));
    check_mem(ttid, "get:malloc:ttid");

    // TODO if numpkts < SERVER_SWND_SIZE

    filedata = (char *)malloc(DATASIZE);
    for (j = 0; j < numpkts; j++) {
        aux = readn(fd, filedata, DATASIZE);
printf("aux %d \n", aux);
printf("lunghezza dati: %lu\n", strlen((char *)filedata));

        sendpkt[j] = makepkt(CARGO, iseq, 0, numpkts - j, aux, filedata);
printf("(sendpkt[%d] SIZE %d, pktleft %d, dati %s \n", j, sendpkt[j].size, sendpkt[j].pktleft, sendpkt[j].data);
        memset(filedata, 0, DATASIZE);
        iseq++;
    }
    close(fd);

    for (z=numpkts-1; z>=0;z--){
        push_pkt(&stackPtr, sendpkt[z]);
    }

    /*****INIZIALIZZAZIONE SEMAFORI E MUTEX**********/
    check(pthread_mutex_init(&mtxTime,NULL),"GET: errore pthread_mutex_init time");
    check(pthread_mutex_init(&mtxStack,NULL),"GET: errore pthread_mutex_init struct stack_elem");
    check(pthread_mutex_init(&mtxAck_counter,NULL),"GET: errore pthread_mutex_init Ack Counters");

    semTimer = check(semget(IPC_PRIVATE,1,IPC_CREAT|IPC_EXCL|0666),"GET: semget semTimer"); //inizializzazione SemTimer
    check(semctl(semTimer,0,SETVAL,0), "GET: semctl semTimer");

    semPkt_to_send = check(semget(IPC_PRIVATE,1,IPC_CREAT|IPC_EXCL|0666),"GET: semget semPkt_to_send"); //inizializzazione semPkt_to_send
    if (simulateloss(0)) check(semctl(semPkt_to_send,0,SETVAL,numpkts), "GET: semctl semPkt_to_send");

    //preparo il t_info da passare ai thread
    t_info.stack = &stackPtr;
    t_info.sem_readypkts = semPkt_to_send;
    t_info.semTimer = semTimer;
    t_info.mutex_time = mtxTime;
    t_info.mutex_stack = mtxStack;
    t_info.mutex_ack_counter = mtxAck_counter;
    t_info.ack_counters = counter;
    t_info.base = &base;
    t_info.initialseq = base;
    t_info.numpkts = numpkts;
    t_info.sockd = opersd;
    t_info.timer = &timer;
    t_info.devRTT = &devRTT;
    t_info.estimatedRTT = &estimatedRTT;
    t_info.startRTT = startRTT;
    t_info.timeout_Interval = &timeout_Interval;
    t_info.father_pid = tid;

    for(j=0;j<SERVER_SWND_SIZE;j++){
        if(pthread_create(&ttid[j], NULL, (void *)thread_sendpkt, (void *)&t_info) != 0){
            fprintf(stderr, "get:pthread_create:thread_sendpkt");
            exit(EXIT_FAILURE);
        }
    }

    memset(&act_lastack, 0, sizeof(struct sigaction));
    act_lastack.sa_handler = &kill_handler;
    sigemptyset(&act_lastack.sa_mask);
    act_lastack.sa_flags = 0;
    check(sigaction(SIGFINAL, &act_lastack, NULL), "get:sigaction:siglastack");

    //signal(/*stop timer-base aggiornata*/);
    while((base-init)<=numpkts){
    /*
        oper.sem_num = 0;
        oper.sem_op = -1;
        oper.sem_flg = SEM_UNDO;

        check(semop(semTimer,&oper,1),"GET: error wait semTimer");   //WAIT su semTimer
        check(pthread_mutex_lock(&mtxTime),"GET: error lock time");
        oldBase=base;
        usleep(timeout_Interval);
        if (counter[oldBase - init]==0){ //if (oldBase==base)   //RITRASMISSIONE
            push_pkt(&stackPtr,sendpkt[oldBase - init]);  //o handler()signal(sem_pkts_to_send)

            oper.sem_num = 0;                                                 //se RITRASMISSIONE
            oper.sem_op = 1;                                                  //signal a semPkt_to_send
            oper.sem_flg = SEM_UNDO;

            check(semop(semPkt_to_send,&oper,1),"GET: error signal semLocal ");

            oper.sem_num = 0;                                                 //se RITRASMISSIONE
            oper.sem_op = 1;                                                  //signal a semGlobal
            oper.sem_flg = SEM_UNDO;

            check(semop(SemSnd_Wndw,&oper,1),"GET: error signal semGlobal ");
        }
        timer=0;
        check(pthread_mutex_unlock(&mtxTime),"GET: error unlock time");
    */
    }
    //return 1;
}

/*
 *  function: put
 *  ----------------------------
 *  Receive a file from the client
 *
 *  arg: synop packet from client, client address
 *
 *  return: -
 *  error: -
 */
void put(void *arg){
    int me = (int)pthread_self();
    struct elab synop = *((struct elab *)arg);
    struct pkt synack, cargo;
    struct receiver_info t_info;
    pthread_mutex_t mutex_rcvqueue;
    struct sigaction act_lastwrite;
    struct sembuf signal_readypkts;
    int n;

    /*** Handshake (no synop) with client ***/
    t_info.sockd = serve_op(&synack, synop);
    if(t_info.sockd < 0){
printf("(Server:put tid:%d) Operation op:%d seq:%d unsuccessful, exiting operation\n\n", me, synop.clipacket.op, synop.clipacket.seq);
        pthread_exit(NULL);
    }
printf("(Server:put tid:%d) Handshake successful, continuing operation\n\n", me);

    /*** Filling info for threads ***/
    t_info.numpkts = synop.clipacket.pktleft;
    t_info.nextseqnum = check_mem(malloc(sizeof(int)), "put:malloc:nextseqnum");
    *t_info.nextseqnum = synack.ack+1;
    t_info.sem_readypkts = check(semget(IPC_PRIVATE, 1, IPC_CREAT|IPC_EXCL|0666), "put:semget:sem_rcvqueue");
    check(semctl(t_info.sem_readypkts, 0, SETVAL, 0), "put:semctl:sem_readypkts");
    t_info.sem_writebase = check(semget(IPC_PRIVATE, 1, IPC_CREAT|IPC_EXCL|0666), "put:semget:sem_writebase");
    check(semctl(t_info.sem_writebase, 0, SETVAL, 0), "put:semctl:sem_writebase");
    check(pthread_mutex_init(&mutex_rcvqueue, NULL), "put:pthread_mutex_init:mutex_rcvqueue");
    t_info.mutex_rcvqueue = mutex_rcvqueue;
    t_info.mutex_rcvbuf = mutex_rcvbuf;
    t_info.received_pkts = check_mem(malloc(sizeof(pktqueue)), "put:malloc:received_pkts");
    init_queue(t_info.received_pkts);
    t_info.file_cells = check_mem(malloc(synack.pktleft * sizeof(int)), "put:malloc:file_cells");
    for(int i=0; i<synack.pktleft; i++){
        t_info.file_cells[i] = -1;
    }
    t_info.init_transfer_seq = synack.seq + 1;
    t_info.rcvbase = check_mem(malloc(sizeof(int)), "put:malloc:rcvbase");
    *t_info.rcvbase = synack.seq + 1;
    t_info.last_packet_size = check_mem(malloc(sizeof(int)), "put:malloc:last_packet_size");
    t_info.filename = synop.clipacket.data;
    server_ttid[CLIENT_NUMTHREADS+1] = pthread_self();

    /*** Creating N threads for receiving and 1 for writing ***/
    for(int i=0; i<SERVER_NUMTHREADS; i++){
        pthread_create(&server_ttid[i], NULL, (void *)receiver, (void *)&t_info);
    }
    pthread_create(&server_ttid[SERVER_NUMTHREADS], NULL, (void *)writer, (void *)&t_info);

    /*** Capture SIGFINAL when writer thread has finished to write onto the file ***/
    memset(&act_lastwrite, 0, sizeof(struct sigaction));
    act_lastwrite.sa_handler = &server_kill_handler;
    sigemptyset(&act_lastwrite.sa_mask);
    act_lastwrite.sa_flags = 0;
    check(sigaction(SIGFINAL, &act_lastwrite, NULL), "put:sigaction:siglastwrite");

receive:
    check_mem(memset((void *)&cargo, 0, sizeof(struct pkt)), "put:memset:ack");
    n = recv(t_info.sockd, &cargo, MAXPKTSIZE, 0);

    if(n==0 || // nothing received
        (cargo.seq - t_info.init_transfer_seq) > t_info.numpkts-1 || // packet with seq out of range
        (cargo.seq - t_info.init_transfer_seq) < ((*t_info.rcvbase)-t_info.init_transfer_seq)-1){ // packet processed yet
        goto receive;
    }
printf("[Server:put tid:%d sockd:%d] Received cargo [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d]\n\n", me, t_info.sockd, cargo.op, cargo.seq, cargo.ack, cargo.pktleft, cargo.size);

    if( (cargo.seq - t_info.init_transfer_seq) == t_info.numpkts-1)
        *t_info.last_packet_size = cargo.size;

    check(enqueue(t_info.received_pkts, cargo), "put:enqueue:cargo");

    signal_readypkts.sem_num = 0;
    signal_readypkts.sem_op = 1;
    signal_readypkts.sem_flg = SEM_UNDO;
    check(semop(t_info.sem_readypkts, &signal_readypkts, 1), "put:semop:signal:sem_readypkts");

    goto receive;
}

// LEGACY
/*void put(void *arg){
    int fd;
    size_t filesize;
    int npkt,edgepkt;
    int pos,lastpktsize;
    char *localpathname;
    struct pkt rack, cargo;

    int opersd; // TMP returned from setop

    pthread_t me = pthread_self();

    npkt = pktleft;
    edgepkt = npkt;
    check(recvfrom(opersd, &rack, MAXPKTSIZE, 0, (struct sockaddr *)&cliaddr, &len), "PUT-server:recvfrom ack-client");
printf("[Server] Received ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", rack.op, rack.seq, rack.ack, rack.pktleft, rack.size, rack.data);

    // strcmp(rack.op, ACK_POS)
    if (strcmp(rack.data, "ok") == 0) {
    	initseqserver = rack.seq;
    	localpathname = malloc((DATASIZE) * sizeof(char));
    	sprintf(localpathname, "%s%s", SERVER_FOLDER, pathname);

receiver:
        while(npkt>0){
            check(recvfrom(opersd,&cargo, MAXPKTSIZE, 0, (struct sockaddr *)&cliaddr, &len), "PUT-server:recvfrom Cargo");
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
}*/

/*
 *  function: createlist
 *  ----------------------------
 *  Print list on
 *
 *  res: pointer to string where the result is stored
 *  path: folder to list
 *
 *  return: -
 *  error: -
 */
 void createlist(const char *path) {

        int fdl;
        int i;
        int n_entry;
        DIR *dirp = NULL;
        struct dirent **filename;

        //PRENDO IL LOCK IL MUTEX
        check(pthread_mutex_lock(&mtxlist),"Server:pthread_mutexlist_lock");

          check_mem(dirp = opendir(path),"list nell'apertura della directory");

          printf("\nCONTENUTO DELLA CARTELLA [%s] \n",path);
          /*Crea un file che contiene la filelist*/

          check(fdl = open("list.txt",O_CREAT | O_RDWR | O_TRUNC,0644),"server:open server_files.txt");

          check(n_entry =scandir(path,&filename,NULL,alphasort) ,"server:scandir");

          for(i = 0; i < n_entry; i++){

            if (strcmp(filename[i]->d_name, ".")>0){
                if(strcmp(filename[i]->d_name, "..")>0){
                    printf("%s \n",filename[i]->d_name);
                    dprintf(fdl,"%s\n",filename[i]->d_name);
                }
            }
          }

          closedir(dirp);
          dirp = NULL;

        //LASCIO IL MUTEX
        check(pthread_mutex_unlock(&mtxlist),"server:pthread_mutex_lock");

 }

/*
 *  function: list
 *  ----------------------------
 *  Execute createlist function and send the result to the client
 *
 *  return: -
 *  error: -
 */
void list(void *arg) {
    struct pkt listpkt;
    struct pkt rcvack;
    struct elab synop = *((struct elab*)arg);
    struct pkt synack;
    int fdl;
    int aux;
    char filedata[DATASIZE];
    int n;
    int opersd;

    opersd = serve_op(&synack, synop);
    if(opersd < 0){
printf("Operation op:%d seq:%d unsuccessful\n", synop.clipacket.op, synop.clipacket.seq);
        pthread_exit(NULL);
    }

    createlist(synop.clipacket.data);
    check(fdl = open("list.txt",O_RDONLY,0644),"server:open server_files.txt");

    aux = read(fdl, filedata, DATASIZE);

    listpkt = makepkt(CARGO, synack.ack + 1, 0, 0, aux, filedata);

printf("[Server] Sending list [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", listpkt.op, listpkt.seq, listpkt.ack, listpkt.pktleft, listpkt.size, (char *)listpkt.data);
	if (simulateloss(0)) check(send(opersd, &listpkt, listpkt.size + HEADERSIZE, 0), "main:send");
printf("[Server] Waiting for ack...\n");
    n = recv(opersd, &rcvack, MAXPKTSIZE, 0);

    if(n<1){
printf("No ack response from client\n");
        close(opersd);
    }
    if(rcvack.ack == listpkt.seq){
printf("listpkt sent successfully \n");
    }else{
printf("There are problems, not response ack from client \n");
    }
}

int main(int argc, char const *argv[]){
    struct pkt synop, ack; // ack only when rejecting packets with bad op code
    struct sockaddr_in cliaddr;
    struct elab opdata;
    pid_t me;
    pthread_t tid;
    int ongoing_operations;
    char *spath = DEFAULT_PATH; // root folder for server

    /*** Usage ***/
    if (argc > 2) {
        fprintf(stderr, "\tPath from argv[1] set, extra parameters are discarded. [Usage]: %s [<path>]\n", argv[0]);
    }

    /*** Init ***/
    system("clear");
    if (argc > 1) spath = (char *)argv[1];
    me = getpid();
    printf("\tWelcome to server-simple app, server #%d. Root folder: %s\n", me, spath);
    memset((void *)&listen_addr, 0, sizeof(struct sockaddr_in));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(SERVER_PORT);
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    connsd = setsock(listen_addr, 0);
    check(bind(connsd, (struct sockaddr *)&listen_addr, sizeof(struct sockaddr)), "main:bind:connsd");
    len = sizeof(struct sockaddr_in);
    ongoing_operations = 0;
    free_pages_rcvbuf = check_mem(malloc(CLIENT_RCVBUFSIZE * sizeof(struct index)), "main:init:malloc:free_pages_rcvbuf");
    init_index_stack(&free_pages_rcvbuf, CLIENT_RCVBUFSIZE);
    check(pthread_mutex_init(&mutex_rcvbuf, NULL), "main:pthread_mutex_init:mutex_rcvbuf");

    SemSnd_Wndw = check(semget(IPC_PRIVATE,1,IPC_CREAT|IPC_EXCL|0666),"main:semget:SemSnd_Wndw");
    check(semctl(SemSnd_Wndw,0,SETVAL,SERVER_SWND_SIZE), "main:semctl:SemSnd_Wndw");

   /*** Receiving synop (max BACKLOG) ***/
    while (1) {
        // Reset address and packet of the last operation
        check_mem(memset(&cliaddr, 0, sizeof(struct sockaddr_in)), "main:memset:cliaddr");
        check_mem(memset(&synop, 0, sizeof(struct pkt)), "main:memset:synop");
        check_mem(memset(&synop.data, 0, DATASIZE), "main:memset:synop");

        printf("\n(Server pid:%d) Waiting for synop...\n", me);
        check(recvfrom(connsd, &synop, MAXPKTSIZE, 0, (struct sockaddr *)&cliaddr, &len), "main:rcvfrom:synop");
printf("[Server pid:%d sockd:%d] Received synop [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n\n", me, connsd, synop.op, synop.seq, synop.ack, synop.pktleft, synop.size, synop.data);

        // TODO if ongoing_operations >= BACKLOG send negative ack and goto recvfrom synop

        // Prepare op for child
        check_mem(memset(&opdata.cliaddr, 0, sizeof(struct sockaddr_in)), "main:memset:opdata.cliaddr");
        check_mem(memset(&opdata.clipacket, 0, sizeof(struct pkt)), "main:memset:opdata.clipacket");
        check_mem(memset(&opdata.clipacket.data, 0, DATASIZE), "main:memset:opdata.clipacket");

        memcpy(&opdata.cliaddr, &cliaddr, len);
        // TODEL printf("opdata.clipacket.data:%s %lu\n", opdata.clipacket.data, strlen(opdata.clipacket.data));
        opdata.clipacket = makepkt(synop.op, synop.seq, synop.ack, synop.pktleft, synop.size, synop.data);
printf("(Server:main pid:%d) Creating elab [addr:%d][port:%d][op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n\n", me, opdata.cliaddr.sin_addr.s_addr, opdata.cliaddr.sin_port, opdata.clipacket.op, opdata.clipacket.seq, opdata.clipacket.ack, opdata.clipacket.pktleft, opdata.clipacket.size, opdata.clipacket.data);

        /*** Operation selection ***/
        switch (opdata.clipacket.op) {

            case SYNOP_LIST:
                pthread_create(&tid, NULL, (void *)list, (void *)&opdata);
                ++ongoing_operations;
printf("(Server:main pid:%d) Passed elab to child %d\n\n", me, ((int)tid));
                break;

            case SYNOP_GET:
                pthread_create(&tid, NULL, (void *)get, (void *)&opdata);
                ++ongoing_operations;
printf("(Server:main pid:%d) Passed elab to child %d\n\n", me, ((int)tid));
                break;

            case SYNOP_PUT:
                pthread_create(&tid, NULL, (void *)put, (void *)&opdata);
                ++ongoing_operations;
printf("(Server:main pid:%d) Passed elab to child %d\n\n", me, ((int)tid));
                break;

            default:
printf("(Server:main pid:%d) Can't handle this packet\n\n", me);
                // polite server: send ack with negative status instead of ignoring
                check_mem(memset(&ack, 0, sizeof(struct pkt)), "main:memset:ack");
                ack = makepkt(ACK_NEG, 0, opdata.clipacket.seq, opdata.clipacket.pktleft, strlen("malformed packet"), "malformed packet");
                if (simulateloss(0)) check(sendto(connsd, &ack, HEADERSIZE + ack.size, 0, (struct sockaddr *)&opdata.cliaddr, sizeof(struct sockaddr_in)), "main:sendto:ack:malformed_packet");
printf("[Server:main pid:%d sockd:%d] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n\n", me, connsd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
                break;
        }
    } // end while

    exit(EXIT_FAILURE);
}
