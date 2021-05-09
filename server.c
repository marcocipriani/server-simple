#include "common.c"
#include "macro.h"

int SemSnd_Wndw;
int connsd;
struct sockaddr_in listen_addr;
socklen_t len;
pthread_mutex_t mutex_list;
char *msg;
void **tstatus;
char *spath = SERVER_FOLDER; // root folder for server
int list_dbit = 0; // dirty bit for list
char rcvbuf[SERVER_RCVBUFSIZE*(DATASIZE)];
index_stack free_pages_rcvbuf;
int receiver_window;
pthread_mutex_t mutex_rcvbuf;
pthread_t get_ttid[SERVER_NUMTHREADS+1], put_ttid[3];

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
    check(send(opersd, &ack, HEADERSIZE + ack.size, 0), "setop:send:ack");
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
void get_kill_handler(){
    for(int i=0;i<SERVER_NUMTHREADS;i++){
        pthread_cancel(get_ttid[i]);
    }
printf("\tServer operation completed\n\n");
    pthread_exit(NULL);
}
void put_kill_handler(){
    for(int i=0;i<2;i++){
        pthread_cancel(put_ttid[i]);
    }
printf("\tServer operation completed\n\n");
    pthread_exit(NULL);
};

/*
 *  function: sender
 *  ----------------------------
 *  Send packet to client and wait for at least an any ack
 *
 *  arg: information about transfer from get
 *
 *  return: -
 *  error: -
 */
void sender(void*arg){
    int me = (int)pthread_self();
    struct sender_info cargo;//t_info
    struct pkt sndpkt;
    struct sembuf sembuf_wait, sembuf_signal;
    cargo = *((struct sender_info *)arg);
    // struct timespec end; // not used
    struct pkt emptypkt;

    emptypkt = makepkt(PING, 0, 0, 0, 0, NULL);
    int opersd = cargo.sockd;

    sembuf_wait.sem_num = 0;
    sembuf_wait.sem_op = -1;
    sembuf_wait.sem_flg = SEM_UNDO;

    sembuf_signal.sem_num = 0;
    sembuf_signal.sem_op = 1;
    sembuf_signal.sem_flg = SEM_UNDO;
printf("sono il sender \n");
printf("(*cargo.nextseqnum) -(*cargo.base): %d<(*cargo.rwnd):\n",(*cargo.base) );
printf("(*(*cargo.rwnd)); %d \n",(*cargo.rwnd));
printf("cargo.array.seq %d, size %d, data %s\n",cargo.array->seq,cargo.array->size,cargo.array->data);
//printf("cargo.array[2].seq %d, size %d, data %s\n",cargo.(array+2).seq,cargo.(array+2).size,cargo.(array+2).data);
     clock_gettime( CLOCK_REALTIME,cargo.time_upload);
//printf("cargo.startRTT->start; %d  %lf per pkt; %d\n",(int)cargo.startRTT.start->tv_sec,(float)(1e-9)*cargo.startRTT.start->tv_nsec,sndpkt.seq-cargo.initialseq);
        *(cargo.startRTT.seq)=sndpkt.seq;
    while(1){
        usleep(500);
        if((*cargo.nextseqnum)-(*cargo.base)<(*cargo.rwnd)){
printf("(*(*cargo.rwnd buona)); %d \n",(*cargo.rwnd));
    printf("thr:attesa Locale semready\n");
            check(semop(cargo.sem_readypkts,&sembuf_wait,1),"THREAD: error wait sem_readypkts");    //wait su semLocale=pkts_to_send
                                                                        //controllo che ci siano pkt da inviare
    printf("thr:attesa globale\n");
            check(semop(SemSnd_Wndw,&sembuf_wait,1),"THREAD: error wait global");    //wait su semGlob
    printf("thr:fermo al mutex_stack\n");

            if(pthread_mutex_lock(&cargo.mutex_stack) != 0) {   //lock sulla struct stack_elem
                fprintf(stderr, "sender:pthread_mutex_lock:mutex_stack\n");
                exit(EXIT_FAILURE);
            }
    printf("ho preso il lock\n");
            int res = check(pop_pkt(cargo.stack,&sndpkt), "sender:pop_pkt:sndpkt");
            if(sndpkt.seq>(*cargo.base)){
                //(*cargo.nextseqnum)++;
                (*cargo.nextseqnum)=sndpkt.seq+1;
            }

    printf("ho fatto una pop %d \n",res);

            if(pthread_mutex_unlock(&cargo.mutex_stack)!=0){   //unlock struct stack_elem
                fprintf(stderr, "sender:pthread_mutex_lock:mutex_stack\n");
                exit(EXIT_FAILURE);
            }
    //printf("ho rilasciato il lock\n");
        //sendto(opersd, &sndpkt, HEADERSIZE + sndpkt.size, 0, (struct sockaddr *)&cliaddr, sizeof(struct sockaddr_in));
            if (simulateloss(0)) check(send(cargo.sockd, &sndpkt, HEADERSIZE + sndpkt.size, 0), "thread_sendpkt:send:cargo");
    printf("[Server tid:%d sockd:%d] Sended packet [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d]\n\n", me, opersd, sndpkt.op, sndpkt.seq, sndpkt.ack, sndpkt.pktleft, sndpkt.size);
        }
        else if(*cargo.rwnd == 0){
printf("(*(*cargo.rwnd cattiva)); %d \n",(*cargo.rwnd));
printf("(*cargo.nextseqnum) -(*cargo.base): %d\n",(*cargo.nextseqnum)-(*cargo.base) );
            if (simulateloss(0)) check(send(cargo.sockd, &emptypkt , HEADERSIZE + sndpkt.size, 0), "thread_sendpkt:send:emptypkt");
            usleep(1000);
        }
        else{
            check(semop(cargo.sem_readypkts,&sembuf_wait,1),"THREAD: error wait sem_readypkts");    //wait su semLocale=pkts_to_send
                                                                        //controllo che ci siano pkt da inviare
    printf("thr:attesa globale\n");
            check(semop(SemSnd_Wndw,&sembuf_wait,1),"THREAD: error wait global");    //wait su semGlob
    printf("thr:fermo al mutex_stack\n");

            if(pthread_mutex_lock(&cargo.mutex_stack) != 0) {   //lock sulla struct stack_elem
                fprintf(stderr, "sender:pthread_mutex_lock:mutex_stack\n");
                exit(EXIT_FAILURE);
            }
            usleep(10000);

            check(pop_pkt(cargo.stack,&sndpkt), "sender:pop_pkt:sndpkt");
            if(sndpkt.seq>(*cargo.base)){
                //(*cargo.nextseqnum)++;
                (*cargo.nextseqnum)=sndpkt.seq+1;
            }

            if(pthread_mutex_unlock(&cargo.mutex_stack)!=0){   //unlock struct stack_elem
                fprintf(stderr, "sender:pthread_mutex_lock:mutex_stack\n");
                exit(EXIT_FAILURE);
            }
printf("invio pacchetto con #seq: %d\n",sndpkt.seq);
            if (simulateloss(0)) check(send(cargo.sockd, &sndpkt, HEADERSIZE + sndpkt.size, 0), "thread_sendpkt:send:cargo");
        }
    }
}

/*
 *  function: ackreceiver
 *  ----------------------------
 *  Receive ack and update receiver base
 *
 *  arg: sinformation about transfer from get
 *
 *  return: -
 *  error: -
 */
void ackreceiver(void *arg){
    int me = (int)pthread_self();
    struct sender_info cargo;//t_info
    struct pkt rcvack;
    int k,n;
    struct sembuf sembuf_wait, sembuf_signal;
    cargo = *((struct sender_info *)arg);
    struct timespec end_upload;
    // struct timespec end;

    int opersd = cargo.sockd;

    sembuf_wait.sem_num = 0;
    sembuf_wait.sem_op = -1;
    sembuf_wait.sem_flg = SEM_UNDO;

    sembuf_signal.sem_num = 0;
    sembuf_signal.sem_op = 1;
    sembuf_signal.sem_flg = SEM_UNDO;
printf("thread recettori\n");
    while(1){
        n = recv(opersd, &rcvack, MAXPKTSIZE, 0);
printf("thr: ho ricevuto qualcosa %d\n",n);
        if(n==0){
printf("thr entra in 0\n");
        }
        if(n==-1){
printf("thr entra in -1\n");
        }

        if(n>0){
printf("thr entra in >0\n");
            if(pthread_mutex_lock(&cargo.mutex_ack_counter) != 0){
            fprintf(stderr, "thread_sendpkt:pthread_mutex_lock:mutex_ack_counter\n");
            exit(EXIT_FAILURE);
            }
printf("sono il thread # %d e' ho ricevuto l'ack del pkt #%d \n", me, (rcvack.ack) - (cargo.initialseq) + 1);
printf("valore di partenza in counter[%d] : %d \n", (rcvack.ack) - (cargo.initialseq), cargo.ack_counters[(rcvack.ack) - (cargo.initialseq)]);

    /*      if(*(cargo.startRTT.seq)==rcvack.ack){
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
            }*/

         //   if(rcvack.ack-(*(cargo.base))>SERVER_SWND_SIZE /*|| rcvack.ack<(*(cargo.base)-1)*/){    //ack fuori //finestra
         //       check(pthread_mutex_unlock(&cargo.mutex_ack_counter),"THREAD: error unlock Ack Counters");
          //      //memset(&rcvack.data,0,DATASIZE);
           // // goto check_ack;
            //}*/

            if(rcvack.ack >= *(cargo.base)){   //ricevo un ack nella finestra
    printf("valore di counter[%d]: base-1: %d \n",*(cargo.base)-1-cargo.initialseq,cargo.ack_counters[*(cargo.base)-1 - (cargo.initialseq)]);
                for (k=*(cargo.base);k<=rcvack.ack;k++){
                    //se pktlft=seq relative..da fare
                    cargo.ack_counters[k - (cargo.initialseq)] = (int)cargo.ack_counters[k - (cargo.initialseq)] + 1; //sottraggo il num.seq iniziale
                    (*(cargo.base))++; //da controllare


                    check(semop(SemSnd_Wndw,&sembuf_signal,1),"THREAD: error signal global at received ack ");
    printf("valore aggiornato in counter[%d] : %d \n", k - (cargo.initialseq), cargo.ack_counters[k - (cargo.initialseq)]);
                }
                //*(cargo.base)=rcvack.ack+1;

                check(pthread_mutex_unlock(&cargo.mutex_ack_counter),"THREAD: error unlock Ack Counters");
                if(rcvack.ack+1 == cargo.initialseq+cargo.numpkts){     //FINE TRASMISSIONE
                    printf("\t(Server:get tid:%d) Received last ack for file\n", me);
                    (*(cargo.base))++;//evito ulteriori (inutile)ritrasmissioni

                    clock_gettime( CLOCK_REALTIME,&end_upload);
                    float uploading = ((end_upload.tv_sec - cargo.time_upload->tv_sec) + (1e-9)*(end_upload.tv_nsec - cargo.time_upload->tv_nsec));
printf("Durata upload: %f ns\n",uploading);
                    pthread_kill(cargo.father_pid, SIGFINAL);
                    pthread_exit(NULL);
                }
                (*cargo.rwnd)= rcvack.pktleft;         //IMPOSTO LA RWND
                //memset(&sndpkt,0,sizeof(struct pkt));
                //memset(&rcvack,0,sizeof(struct pkt));
                //goto transmit;
            }

            else if(rcvack.ack<=(*cargo.base)-1 ){   //ack duplicato   //manca da aumentare counter
                if ((cargo.ack_counters[(rcvack.ack) - (cargo.initialseq)]) >= 3) { // 3 duplicated acks
                    //(*cargo.base)=rcvack.ack+1;
    printf("dovrei fare una fast retransmit del pkt con #seg: %d\n", rcvack.ack);
                    (cargo.ack_counters[(rcvack.ack) - (cargo.initialseq)]) = 1;//(cargo.p[(rcvack.ack) - (cargo.initialseq)]) + 1;
    printf("azzero il counter[%d] : %d \n", (rcvack.ack) - (cargo.initialseq), cargo.ack_counters[(rcvack.ack) - (cargo.initialseq)]);

                    if(pthread_mutex_unlock(&cargo.mutex_ack_counter)!= 0){
                        fprintf(stderr, "thread_sendpkt:pthread_mutex_lock:mutex_ack_counter\n");
                        exit(EXIT_FAILURE);
                    }

                    if(pthread_mutex_lock(&cargo.mutex_stack) != 0){
                        fprintf(stderr, "thread_sendpkt:pthread_mutex_lock:mutex_stack\n");
                        exit(EXIT_FAILURE);
                    }

  /*                  check(push_pkt(cargo.stack, cargo.array[(*cargo.base)-1]), "thread_sendpkt:push_pkt:stack");
    printf("(Server:thread_sendpkt tid%d) Locked the stack to put pkt after retransmit and pushed the packet seq:%d back into the stack\n\n", me, sndpkt.seq);
                    //check(semop(cargo.sem_readypkts, &sembuf_signal, 1),"thread_sendpkt:semop:signal:sem_readypkts");
                    //check(semop(SemSnd_Wndw, &sembuf_signal, 1),"thread_sendpkt:semop:signal:SemSnd_Wndw");

                    if(pthread_mutex_unlock(&cargo.mutex_stack) != 0){
                        fprintf(stderr, "thread_sendpkt:pthread_mutex_unlock:mutex_stack\n");
                        exit(EXIT_FAILURE);
                    }
    */

                    // poking the next thread waiting on transmit
                    //check(pthread_mutex_unlock(&cargo.mutex_ack_counter),"THREAD: error unlock Ack Counters");
                    //check(semop(cargo.sem_readypkts, &sembuf_signal, 1),"thread_sendpkt:semop:signal:sem_readypkts");
                    //check(semop(SemSnd_Wndw, &sembuf_signal, 1),"thread_sendpkt:semop:signal:sem_readypkts");
                } else {
                    if(rcvack.ack==cargo.initialseq-1){
                    }// goto transmit;
                    (cargo.ack_counters[(rcvack.ack) - (cargo.initialseq)])=(int)(cargo.ack_counters[(rcvack.ack) - (cargo.initialseq)])+1;
                    //*(cargo.base)=rcvack.ack+1;
                    check(pthread_mutex_unlock(&cargo.mutex_ack_counter),"THREAD: error unlock Ack Counters");
                    //memset(&rcvack,0,sizeof(struct pkt));
                    //goto check_ack;
                }
            }
        } // end if(n>0)//byte ricevuti da recv
        else if((*(cargo.base))<cargo.numpkts+cargo.initialseq){
printf("rec in elseif\n");
            //check(semop(cargo.semTimer,&sembuf_signal,1),"THREAD: error signal global at received nothing ");
        //check(pthread_mutex_unlock(&cargo.mutex_ack_counter),"THREAD: error unlock Ack Counters-1");
        //goto transmit;
        }
        else {
printf("rec in else\n");
            //heck(pthread_mutex_unlock(&cargo.mutex_ack_counter),"THREAD: error unlock Ack Counters-2");
        }
printf("rec fuori dal while\n");
        //check(pthread_mutex_unlock(&cargo.mutex_ack_counter),"THREAD: error unlock Ack Counters-2");
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
void get(void *arg){
    int me = (int)pthread_self();
    struct pkt *sendpkt;
    int fd;
    int j, z;
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
    int rwnd=SERVER_SWND_SIZE;
    struct timespec upload;

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
    int nextseqnum = synack.ack + 1;

    startRTT.start=&start;
    startRTT.seq=&rtt;
    estimatedRTT=2000;
    devRTT=500;
    //filename=(char *)malloc((synop.clipacket.size+1)*(sizeof(char)));
    //strncpy(filename,synop.clipacket.data,(size_t)synop.clipacket.size);
    localpathname = (char *)malloc((DATASIZE) * sizeof(char));
    sprintf(localpathname, "%s%s", SERVER_FOLDER, synop.clipacket.data);
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
        check(push_pkt(&stackPtr, sendpkt[z]), "get:push_pkt:sendpkt[z]");
    }

    /*****INIZIALIZZAZIONE SEMAFORI E MUTEX**********/
    check(pthread_mutex_init(&mtxTime,NULL),"GET: errore pthread_mutex_init time");
    check(pthread_mutex_init(&mtxStack,NULL),"GET: errore pthread_mutex_init struct stack_elem");
    check(pthread_mutex_init(&mtxAck_counter,NULL),"GET: errore pthread_mutex_init Ack Counters");

    semTimer = check(semget(IPC_PRIVATE,1,IPC_CREAT|IPC_EXCL|0666),"GET: semget semTimer"); //inizializzazione SemTimer
    check(semctl(semTimer,0,SETVAL,0), "GET: semctl semTimer");

    semPkt_to_send = check(semget(IPC_PRIVATE,1,IPC_CREAT|IPC_EXCL|0666),"GET: semget semPkt_to_send"); //inizializzazione semPkt_to_send
    check(semctl(semPkt_to_send,0,SETVAL,numpkts), "GET: semctl semPkt_to_send");

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
    t_info.array = sendpkt;
    t_info.rwnd = &rwnd;
    t_info.nextseqnum = &nextseqnum;
    t_info.time_upload = &upload;
printf("ho creato la struttura ai thread \n");

 /*   for(j=0;j<SERVER_SWND_SIZE;j++){
        if(pthread_create(&get_ttid[j], NULL, (void *)thread_sendpkt, (void *)&t_info) != 0){
            fprintf(stderr, "get:pthread_create:thread_sendpkt");
            exit(EXIT_FAILURE);
        }
    }
    */
     if(pthread_create(&get_ttid[SERVER_NUMTHREADS+1], NULL, (void *)sender, (void *)&t_info) != 0){
            fprintf(stderr, "get:pthread_create:thread_sendpkt");
            exit(EXIT_FAILURE);
        }
printf(" creato il send thread \n");
    for(j=0;j<SERVER_NUMTHREADS;j++){
        if(pthread_create(&get_ttid[j], NULL, (void *)ackreceiver, (void *)&t_info) != 0){
            fprintf(stderr, "get:pthread_create:thread_sendpkt");
            exit(EXIT_FAILURE);
        }
    }
printf("creati i rec ack thread \n");

    memset(&act_lastack, 0, sizeof(struct sigaction));
    act_lastack.sa_handler = &get_kill_handler;
    sigemptyset(&act_lastack.sa_mask);
    act_lastack.sa_flags = 0;
    check(sigaction(SIGFINAL, &act_lastack, NULL), "get:sigaction:siglastack");

    //signal(/*stop timer-base aggiornata*/);
    while((base-init)<=numpkts){
        usleep(2000);
       /* oper.sem_num = 0;
        oper.sem_op = -1;
        oper.sem_flg = SEM_UNDO;

        check(semop(semTimer,&oper,1),"GET: error wait semTimer");   //WAIT su semTimer*/
        //check(pthread_mutex_lock(&mtxTime),"GET: error lock time");
        oldBase=base;
printf("prima di dormire: oldbase %d\n",oldBase);
        usleep(timeout_Interval);
printf("babbo si è svegliato: oldbase %d, newbase %d\n",oldBase,base);
        if (oldBase==base) {  //RITRASMISSIONEif (counter[oldBase - init]==0){
            check(pthread_mutex_lock(&mtxStack),"GET: error lock stack");
            check(push_pkt(&stackPtr,sendpkt[oldBase - init]), "get:push_pkt:sendpkt[oldBase-init]");
printf("HO PUSHATO PKT %d, relativo %d\n",oldBase, oldBase -init);
            check(pthread_mutex_unlock(&mtxStack),"GET: error lock stack");
printf("BABBO HA LIBERATO IL LOCK ALLA PILA\n");

            oper.sem_num = 0;                                                 //se RITRASMISSIONE
            oper.sem_op = 1;                                                  //signal a semPkt_to_send
            oper.sem_flg = SEM_UNDO;

            check(semop(semPkt_to_send,&oper,1),"GET: error signal semLocal ");
printf("BABBO alza semLocale\n");
            oper.sem_num = 0;                                                 //se RITRASMISSIONE
            oper.sem_op = 1;                                                  //signal a semGlobal
            oper.sem_flg = SEM_UNDO;

            check(semop(SemSnd_Wndw,&oper,1),"GET: error signal semGlobal ");
printf("BABBO alza semGlobale\n");
        }
        timer=0;
        //check(pthread_mutex_unlock(&mtxTime),"GET: error unlock time");

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

    sprintf(localpathname, "%s%s", SERVER_FOLDER, info.filename);
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
            receiver_window++;
printf("Receiver window:%d ++\n", receiver_window);
            info.file_cells[last_write_made]=-1;
            last_write_made = last_write_made +1;
printf("(Server:writer tid:%d) Written %d bytes from rcvbuf[%d] to %s\n\n", me, n_bytes_to_write, free_index, localpathname);
            if(last_write_made == info.numpkts) break;
        }

        pthread_mutex_unlock(&info.mutex_rcvbuf);
    }

    close(fd);
    free(localpathname);
    pthread_kill(put_ttid[2], SIGFINAL);
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
    struct pkt cargo, ack, ping, fin;
    int i;
    struct receiver_info info = *((struct receiver_info *)arg);

    signal_writebase.sem_num = 0;
    signal_writebase.sem_op = 1;
    signal_writebase.sem_flg = SEM_UNDO;
    wait_readypkts.sem_num = 0;
    wait_readypkts.sem_op = -1;
    wait_readypkts.sem_flg = SEM_UNDO;

waitforpkt:
    // wait if there are packets to be read
    check(semop(info.sem_readypkts, &wait_readypkts, 1), "receiver:semop:sem_readypkts");

    pthread_mutex_lock(&info.mutex_rcvqueue);
    if(dequeue(info.received_pkts, &cargo) == -1){
        fprintf(stderr, "Can't dequeue packet from received_pkts\n");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_lock(&info.mutex_rcvbuf);

    if(cargo.op == PING){
        ping = makepkt(ACK_POS, *info.nextseqnum, (*info.rcvbase)-1, receiver_window, strlen(RECEIVER_WINDOW_STATUS), RECEIVER_WINDOW_STATUS);
printf("[Server:receiver tid:%d sockd:%d] Sending ping-receiver_window [op:%d][seq:%d][ack:%d][pktleft/rwnd:%d][size:%d][data:%s]\n\n", me, info.sockd, ping.op, ping.seq, ping.ack, ping.pktleft, ping.size, (char *)ping.data);
        if (simulateloss(0)) check(send(info.sockd, &ping, HEADERSIZE + ack.size, 0) , "receiver:send:ping-receiver_window");
        check_mem(memset((void *)&ping, 0, MAXPKTSIZE), "receiver:memset:ping");
        goto waitforpkt;
    }

printf("\t\tcargo.seq:%d info.rcvbase:%d\nn", cargo.seq, *info.rcvbase);

    if(info.file_cells[cargo.seq - info.init_transfer_seq] == -1){ // packet still not processed
        i = check(pop_index(&free_pages_rcvbuf), "receiver:pop_index:free_pages_rcvbuf");
        receiver_window--;
printf("Receiver window:%d --\n", receiver_window);
        check_mem(memcpy(&rcvbuf[i*(DATASIZE)], &cargo.data, cargo.size), "receiver:memcpy:cargo");
        info.file_cells[cargo.seq-info.init_transfer_seq] = i;
printf("(Server:receiver tid:%d) Dequeued %d packet and stored it in rcvbuf[%d]\n\n", me, cargo.seq-info.init_transfer_seq, i);

        if(cargo.seq == *info.rcvbase){
            (*info.nextseqnum)++;
            while(info.file_cells[(*info.rcvbase)-info.init_transfer_seq] != -1){
                (*info.rcvbase)++; // increase rcvbase for every packet already processed
                if((*info.rcvbase)-info.init_transfer_seq == info.numpkts) break;
            }
            ack = makepkt(ACK_POS, *info.nextseqnum, (*info.rcvbase)-1, receiver_window, strlen(CARGO_OK), CARGO_OK);
printf("[Server:receiver tid:%d sockd:%d] Sending ack-newbase [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n\n", me, info.sockd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
            if (simulateloss(0)) check(send(info.sockd, &ack, HEADERSIZE + ack.size, 0) , "receiver:send:ack-newbase");

            // tell the thread doing writer to write base cargo packet
            check(semop(info.sem_writebase, &signal_writebase, 1), "receiver:semop:signal:sem_writebase");

            // send fin to server
            if((*info.rcvbase)-info.init_transfer_seq == info.numpkts){
                (*info.nextseqnum)++;
                fin = makepkt(FIN, *info.nextseqnum, (*info.rcvbase)-1, 0, strlen(FIN_MSG), FIN_MSG);
printf("[Client:receiver tid:%d sockd:%d] Sending fin [op:%d][seq:%d][ack:%d][size:%d][data:%s]\n\n", me, info.sockd, fin.op, fin.seq, fin.ack, fin.size, (char *)fin.data);
                check(send(info.sockd, &fin, HEADERSIZE+fin.size, 0) , "receiver:send:fin");
            }
        }else{
            (*info.nextseqnum)++;
            ack = makepkt(ACK_POS, *info.nextseqnum, (*info.rcvbase)-1, receiver_window, strlen(CARGO_MISSING), CARGO_MISSING);
printf("[Server:receiver tid:%d sockd:%d] Sending ack-missingcargo [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n\n", me, info.sockd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
            check(send(info.sockd, &ack, HEADERSIZE + ack.size, 0) , "receiver:send:ack-missingcargo");
        }
    }else{
        ack = makepkt(ACK_POS, *info.nextseqnum, (*info.rcvbase)-1, receiver_window, strlen(CARGO_MISSING), CARGO_MISSING);
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
    struct pkt synack, cargo, ack;
    struct receiver_info t_info;
    pthread_mutex_t mutex_rcvqueue;
    struct sigaction act_lastwrite;
    struct sembuf signal_readypkts;
    int n;

    /*** Handshake (no synop) with client ***/
    t_info.sockd = serve_op(&synack, synop);
    if(t_info.sockd < 1){
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
    t_info.file_cells = check_mem(malloc(t_info.numpkts * sizeof(int)), "put:malloc:file_cells");
    for(int i=0; i<t_info.numpkts; i++){
        t_info.file_cells[i] = -1;
    }
    t_info.init_transfer_seq = synack.seq + 1;
    t_info.rcvbase = check_mem(malloc(sizeof(int)), "put:malloc:rcvbase");
    *t_info.rcvbase = synack.seq + 1;
    t_info.last_packet_size = check_mem(malloc(sizeof(int)), "put:malloc:last_packet_size");
    t_info.filename = synop.clipacket.data;
    put_ttid[2] = pthread_self();

    /*** Creating N threads for receiving and 1 for writing ***/
    //for(int t=0; t<SERVER_NUMTHREADS; t++){
    if(pthread_create(&put_ttid[0], NULL, (void *)receiver, (void *)&t_info) != 0){
        fprintf(stderr, "put:pthread_create:receiver");
        exit(EXIT_FAILURE);
    }
    if(pthread_create(&put_ttid[1], NULL, (void *)writer, (void *)&t_info) != 0){
        fprintf(stderr, "put:pthread_create:writer");
        exit(EXIT_FAILURE);
    }

    /*** Capture SIGFINAL when writer thread has finished to write onto the file ***/
    memset(&act_lastwrite, 0, sizeof(struct sigaction));
    act_lastwrite.sa_handler = &put_kill_handler;
    sigemptyset(&act_lastwrite.sa_mask);
    act_lastwrite.sa_flags = 0;
    check(sigaction(SIGFINAL, &act_lastwrite, NULL), "put:sigaction:siglastwrite");

receive:
    check_mem(memset((void *)&cargo, 0, sizeof(struct pkt)), "put:memset:ack");
    n = recv(t_info.sockd, &cargo, MAXPKTSIZE, 0);

printf("(Server:put tid%d) Actual base is %d\n", me, *t_info.rcvbase);

    if(n==0 || // nothing received
        (cargo.seq - t_info.init_transfer_seq) > t_info.numpkts-1 || // packet with seq out of range
        (cargo.seq - t_info.init_transfer_seq) < ((*t_info.rcvbase)-t_info.init_transfer_seq)){ // packet processed yet
        ack = makepkt(ACK_NEG, *t_info.nextseqnum, (*t_info.rcvbase)-1, receiver_window, strlen(CARGO_MISSING), CARGO_MISSING);
        if (simulateloss(0)) check(send(t_info.sockd, &ack, HEADERSIZE+ack.size, 0) , "put:send:ack-newbase");
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

    free(t_info.nextseqnum);
    free(t_info.received_pkts);
    free(t_info.file_cells);
    free(t_info.rcvbase);
    free(t_info.last_packet_size);
    pthread_exit(NULL);
}

/*
 *  function: createlist
 *  ----------------------------
 *  Create list from folder
 *
 *  path: folder to list
 *
 *  return: -
 *  error: -
 */
void createlist(const char *path) {
    DIR *folder;
    struct dirent **filename;
    int fd;
    int n;

    if(pthread_mutex_lock(&mutex_list) !=0){
        fprintf(stderr, "createlist:pthread_mutex_lock:mutex_list\n");
        exit(EXIT_FAILURE);
    }

    folder = check_mem(opendir(path), "createlist:opendir:path");
    fd = check(open(SERVER_LIST_FILE, O_CREAT|O_RDWR|O_TRUNC, 0666), "createlist:open:list");

    n = check(scandir(path, &filename, NULL, alphasort), "createlist:scandir");

    for(int i=2; i<n; i++){ // ignore . and ..
        dprintf(fd, "%s\n", filename[i]->d_name);
    }

    closedir(folder);
    folder = NULL;

    if(pthread_mutex_unlock(&mutex_list) !=0){
        fprintf(stderr, "createlist:pthread_mutex_unlock:mutex_list\n");
        exit(EXIT_FAILURE);
    }
    printf("Created new server list\n");
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
    int me = (int)pthread_self();
    struct elab synop = *((struct elab*)arg);
    int opersd;
    struct pkt synack, cargo, ack;
    int fd, listsize;
    char listdata[DATASIZE];
    int n;

    opersd = serve_op(&synack, synop);
    if(opersd < 1){
printf("(Server:list tid:%d) Operation op:%d seq:%d unsuccessful, exiting operation\n\n", me, synop.clipacket.op, synop.clipacket.seq);
        pthread_exit(NULL);
    }
printf("(Server:put tid:%d) Handshake successful, continuing operation\n\n", me);

    list_dbit = 1; // TMP set to 0 after every create list, to 1 or more after every put completed
    if(list_dbit){
        createlist(synop.clipacket.data);
        list_dbit = 0;
    }

    fd = check(open(SERVER_LIST_FILE, O_RDONLY, 0644), "list:open:list.txt");
    listsize = check(read(fd, listdata, DATASIZE), "list:read:fd");
    cargo = makepkt(CARGO, synack.ack + 1, synack.seq, 0, listsize, listdata);

sendlist:
printf("[Server:list tid:%d sockd:%d] Sending list [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, opersd, cargo.op, cargo.seq, cargo.ack, cargo.pktleft, cargo.size, (char *)cargo.data);
    if (simulateloss(0)) check(send(opersd, &cargo, HEADERSIZE+cargo.size, 0), "list:send:cargo");

    n = recv(opersd, &ack, MAXPKTSIZE, 0);

    if(n>1){
printf("[Server:list tid:%d sockd:%d] Received ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n\n", me, opersd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, ack.data);
        if(ack.op == ACK_NEG){
            goto sendlist;
        }
    }

printf("\tOperation seq:%d completed successfully\n", synop.clipacket.seq);
    close(opersd);
    pthread_exit(NULL);
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
    receiver_window = SERVER_RCVBUFSIZE;
    check(pthread_mutex_init(&mutex_rcvbuf, NULL), "main:pthread_mutex_init:mutex_rcvbuf");
    check(pthread_mutex_init(&mutex_list, NULL), "main:pthread_mutex_init:mutex_list");
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
                check(sendto(connsd, &ack, HEADERSIZE + ack.size, 0, (struct sockaddr *)&opdata.cliaddr, sizeof(struct sockaddr_in)), "main:sendto:ack:malformed_packet");
printf("[Server:main pid:%d sockd:%d] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n\n", me, connsd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
                break;
        }
    } // end while

    exit(EXIT_FAILURE);
}
