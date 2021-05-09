#include "macro.h"
#include "common.c"

struct sockaddr_in main_servaddr;
struct sockaddr_in cliaddr; //TODEL
socklen_t len;
int initseqserver; // TODEL
void **tstatus;
int sem_sender_wnd; // global semaphore for sending packets
char rcvbuf[CLIENT_RCVBUFSIZE*(DATASIZE)]; // if local
index_stack free_pages_rcvbuf;
int receiver_window;
pthread_mutex_t mutex_rcvbuf; // mutex for access to receive buffer and free rcvbuf indexes stack
//pthread_mutex_t write_sem; // lock for rcvbuf, free_cells and file_counter
pthread_mutex_t mutex_list; // lock for writing on list.txt
pthread_t ttid[CLIENT_NUMTHREADS + 2]; // TMP not to do in global, pass it to exit_handler

/*
 *  function: request_op
 *  ----------------------------
 *  Check operation validity with server
 *
 *  synack: storing synack pkt
 *  cmd: SYNOP_ABORT, SYNOP_LIST, SYNOP_GET, SYNOP_PUT
 *  pktleft: (only for put) how many packets the file is made of
 *  arg: (list) not-used (get) name of the file to get (put) name of the file to put
 *
 *  return: sockd, synack in opdata param
 *  error: 0
 */
int request_op(struct pkt *synack, int cmd, int pktleft, char *arg){
    int me = (int)pthread_self();
    int sockd;
    struct pkt synop, ack;
    struct sockaddr_in child_servaddr;
    int initseq;
    int n, input;

    sockd = check(setsock(main_servaddr, CLIENT_TIMEOUT), "request_op:setsock");

    //initseq = arc4random_uniform(MAXSEQNUM);
    srand((unsigned int)time(0));
    initseq=rand()%100;
    check_mem(memset((void *)&synop, 0, sizeof(struct pkt)), "request_op:memset:synop");
    check_mem(memset((void *)&ack, 0, sizeof(struct pkt)), "request_op:memset:ack");
    synop = makepkt(cmd, initseq, 0, pktleft, strlen(arg), arg);

printf("[Client:request_op tid:%d sockd:%d] Sending synop [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n\n", me, sockd, synop.op, synop.seq, synop.ack, synop.pktleft, synop.size, (char *)synop.data);
    /*if (simulateloss(1))*/ check(sendto(sockd, &synop, HEADERSIZE + synop.size, 0, (struct sockaddr *)&main_servaddr, sizeof(struct sockaddr_in)) , "request_op:sendto:synop");

    printf("\tWaiting for ack in max %d seconds...\n\n", CLIENT_TIMEOUT);
    n = recvfrom(sockd, (struct pkt *)&ack, MAXPKTSIZE, 0, (struct sockaddr *)&child_servaddr, &len);

    if(n<1){
        // TODO retry op
        printf("\tNo ack response from server\n");
        close(sockd);
        return -1;
    }
printf("[Client:request_op tid:%d sockd:%d] Received ack from server [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n\n", me, sockd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);

    if(ack.op == ACK_NEG && ack.ack == synop.seq){
        printf("\tOperation on server denied\n");
        *synack = makepkt(ACK_NEG, initseq, ack.seq, ack.pktleft, strlen(synop.data), synop.data);
printf("[Client:request_op tid:%d sockd:%d] Sending synack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n\n", me, sockd, synack->op, synack->seq, synack->ack, synack->pktleft, synack->size, (char *)synack->data);
        /*if (simulateloss(1))*/ check(sendto(sockd, synack, HEADERSIZE + synack->size, 0, (struct sockaddr *)&child_servaddr, len) , "request_op:send:server denied");
        close(sockd);
        return -1;
    }

    if(ack.op == ACK_POS && ack.ack == synop.seq){
        printf("\tOperation op:%d seq:%d permitted, estimated packets: %d\n\tContinue? [Y/n] ", synop.op, synop.seq, ack.pktleft);
		input = getchar();
        if(input=='n'){
            cmd = ACK_NEG;
        }else{
            cmd = ACK_POS;
            check(connect(sockd, (struct sockaddr *)&child_servaddr, len), "request_op:connect:child_servaddr");
        }

        initseq++;
        *synack = makepkt(cmd, initseq, ack.seq, ack.pktleft, strlen(synop.data), synop.data);

printf("[Client:request_op tid:%d sockd:%d] Sending synack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n\n", me, sockd, synack->op, synack->seq, synack->ack, synack->pktleft, synack->size, (char *)synack->data);
        /*if (simulateloss(1))*/  check(send(sockd, synack, HEADERSIZE + synack->size, 0) , "request_op:send:synack");
    }

    return sockd;
}

/*
 *  function: kill_handler
 *  ----------------------------
 *  Complete operation terminating every thread involved
 *
 *  return: -
 *  error: -
 */
void kill_handler(){
    //struct receiver_info info = *((struct receiver_info *)arg); // for ttid

    for(int i=0;i<CLIENT_NUMTHREADS;i++){
        pthread_cancel(ttid[i]);
    }
printf("\tClient operation completed\n\n");
    pthread_exit(NULL);
};

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
    fd = check(open(localpathname, O_RDWR|O_CREAT|O_TRUNC, 0666), "writer:open:localpathname");
printf("(Client:writer tid:%d) Opened localpathname:%s\n\n", me, localpathname);

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
            writen(fd, &rcvbuf[free_index*(DATASIZE)], n_bytes_to_write);

            check_mem(memset(&rcvbuf[free_index*(DATASIZE)], 0, DATASIZE), "receiver:memset:rcvbuf[free_index]");
            check(push_index(&free_pages_rcvbuf, free_index), "receiver:push_index");
            receiver_window++;
printf("Receiver window:%d ++\n", receiver_window);
            info.file_cells[last_write_made]=-1;
            last_write_made = last_write_made +1;
printf("(Client:writer tid:%d) Written %d bytes from rcvbuf[%d] to %s\n\n", me, n_bytes_to_write, free_index, localpathname);
            if(last_write_made == info.numpkts) break;
        }

        pthread_mutex_unlock(&info.mutex_rcvbuf);
    }

    close(fd);
    free(localpathname);
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
    struct pkt cargo, ack, ping;
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
        exit(EXIT_FAILURE);
    }
    usleep(1000); // TMP
    pthread_mutex_lock(&info.mutex_rcvbuf);

    if(cargo.seq == PING){ // TODO last scenario of next if
        ping = makepkt(ACK_POS, *info.nextseqnum, (*info.rcvbase)-1, receiver_window, strlen(RECEIVER_WINDOW_STATUS), RECEIVER_WINDOW_STATUS);
printf("[Client:receiver tid:%d sockd:%d] Sending ping-receiver_window [op:%d][seq:%d][ack:%d][pktleft/rwnd:%d][size:%d][data:%s]\n\n", me, info.sockd, ping.op, ping.seq, ping.ack, ping.pktleft, ping.size, (char *)ping.data);
        if (simulateloss(1)) check(send(info.sockd, &ping, HEADERSIZE + ack.size, 0) , "receiver:send:ping-receiver_window");
        check_mem(memset((void *)&ping, 0, MAXPKTSIZE), "receiver:memset:ping");
        goto waitforpkt;
    }

    if(info.file_cells[cargo.seq - info.init_transfer_seq] == -1){ // packet still not processed
        i = check(pop_index(&free_pages_rcvbuf), "receiver:pop_index:free_pages_rcvbuf");
        receiver_window--;
printf("Receiver window:%d --\n", receiver_window);
        check_mem(memcpy(&rcvbuf[i*(DATASIZE)], &cargo.data, cargo.size), "receiver:memcpy:cargo");
        info.file_cells[cargo.seq-info.init_transfer_seq] = i;
printf("(Client:receiver tid:%d) Dequeued %d packet and stored it in rcvbuf[%d]\n\n", me, cargo.seq-info.init_transfer_seq, i);

        if(cargo.seq == *info.rcvbase){
            (*info.nextseqnum)++; // TODO still necessary
            while(info.file_cells[(*info.rcvbase)-info.init_transfer_seq] != -1){
                (*info.rcvbase)++; // increase rcvbase for every packet already processed
                if((*info.rcvbase)-info.init_transfer_seq == info.numpkts) break;
            }
            ack = makepkt(ACK_POS, *info.nextseqnum, (*info.rcvbase)-1, receiver_window, strlen(CARGO_OK), CARGO_OK);
printf("[Client:receiver tid:%d sockd:%d] Sending ack-newbase [op:%d][seq:%d][ack:%d][pktleft/rwnd:%d][size:%d][data:%s]\n\n", me, info.sockd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
            if (simulateloss(1)) check(send(info.sockd, &ack, HEADERSIZE + ack.size, 0) , "receiver:send:ack-newbase");

            // tell the thread doing writer to write base cargo packet
            signal_writebase.sem_num = 0;
            signal_writebase.sem_op = 1;
            signal_writebase.sem_flg = SEM_UNDO;
            check(semop(info.sem_writebase, &signal_writebase, 1), "get:semop:signal:sem_writebase");
        }else{
            (*info.nextseqnum)++; // TODO still necessary
            ack = makepkt(ACK_POS, *info.nextseqnum, (*info.rcvbase)-1, receiver_window, strlen(CARGO_MISSING), CARGO_MISSING);
printf("[Client:receiver tid:%d sockd:%d] Sending ack-missingcargo [op:%d][seq:%d][ack:%d][pktleft/rwnd:%d][size:%d][data:%s]\n\n", me, info.sockd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
            if (simulateloss(1)) check(send(info.sockd, &ack, HEADERSIZE + ack.size, 0) , "receiver:send:ack-missingcargo");
        }
    }else{
        ack = makepkt(ACK_POS, *info.nextseqnum, (*info.rcvbase)-1, receiver_window, strlen(CARGO_MISSING), CARGO_MISSING);
printf("[Client:receiver tid:%d sockd:%d] Sending ack-missingcargo [op:%d][seq:%d][ack:%d][pktleft/rwnd:%d][size:%d][data:%s]\n\n", me, info.sockd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
        if (simulateloss(1)) check(send(info.sockd, &ack, HEADERSIZE + ack.size, 0) , "receiver:send:ack-missingcargo");
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
 *  Download a file from the server
 *
 *  arg: filename to download
 *
 *  return: -
 *  error: -
 */
void get(void *arg){
    int me = (int)pthread_self();
    struct pkt synack, cargo,ack;
    struct receiver_info t_info;
    pthread_mutex_t mutex_rcvqueue;
    struct sigaction act_lastwrite;
    int n;
    struct sembuf signal_readypkts;

    t_info.sockd = request_op(&synack, SYNOP_GET, 0, (char *)arg);
    if(t_info.sockd < 1){
printf("(Client:get tid:%d) Handshake unsuccessful, exiting operation\n\n", me);
        pthread_exit(NULL);
    }
printf("(Client:get tid:%d) Handshake successful, continuing operation\n\n", me);

    t_info.numpkts = synack.pktleft;
    t_info.nextseqnum = check_mem(malloc(sizeof(int)), "get:malloc:nextseqnum");
    *t_info.nextseqnum = synack.seq+1;
    t_info.sem_readypkts = check(semget(IPC_PRIVATE, 1, IPC_CREAT|IPC_EXCL|0666), "get:semget:sem_rcvqueue");
    check(semctl(t_info.sem_readypkts, 0, SETVAL, 0), "get:semctl:sem_readypkts");
    t_info.sem_writebase = check(semget(IPC_PRIVATE, 1, IPC_CREAT|IPC_EXCL|0666), "get:semget:sem_writebase");
    check(semctl(t_info.sem_writebase, 0, SETVAL, 0), "get:semctl:sem_writebase");
    check(pthread_mutex_init(&mutex_rcvqueue, NULL), "get:pthread_mutex_init:mutex_rcvqueue");
    t_info.mutex_rcvqueue = mutex_rcvqueue;
    t_info.mutex_rcvbuf = mutex_rcvbuf;
    t_info.received_pkts = check_mem(malloc(sizeof(pktqueue)), "get:malloc:received_pkts");
    init_queue(t_info.received_pkts);
    t_info.file_cells = check_mem(malloc(synack.pktleft * sizeof(int)), "get:malloc:file_cells");
    for(int i=0; i<synack.pktleft; i++){
        t_info.file_cells[i] = -1;
    }
    // TODO can we use calloc that initialize file_cells with 0s? instead of malloc + for i [i] = -1
    t_info.init_transfer_seq = synack.ack + 1;
    t_info.rcvbase = check_mem(malloc(sizeof(int)), "get:malloc:rcvbase");
    *t_info.rcvbase = synack.ack + 1;
    t_info.last_packet_size = check_mem(malloc(sizeof(int)), "get:malloc:last_packet_size");
    t_info.filename = (char *)arg;

    for(int t=0; t<1/*CLIENT_NUMTHREADS*/; t++){
        if(pthread_create(&ttid[t], NULL, (void *)receiver, (void *)&t_info) != 0){
            fprintf(stderr, "put:pthread_create:receiver%d", t);
            exit(EXIT_FAILURE);
        }
    }
    if(pthread_create(&ttid[CLIENT_NUMTHREADS], NULL, (void *)writer, (void *)&t_info) != 0){
        fprintf(stderr, "put:pthread_create:writer");
        exit(EXIT_FAILURE);
    }

    memset(&act_lastwrite, 0, sizeof(struct sigaction));
    act_lastwrite.sa_handler = &kill_handler;
    sigemptyset(&act_lastwrite.sa_mask);
    act_lastwrite.sa_flags = 0;
    check(sigaction(SIGFINAL, &act_lastwrite, NULL), "get:sigaction:siglastwrite");

    ttid[CLIENT_NUMTHREADS+1] = pthread_self();

receive:
    check_mem(memset((void *)&cargo, 0, sizeof(struct pkt)/*HEADERSIZE + synack.size*/), "get:memset:ack");
    n = recv(t_info.sockd, &cargo, MAXPKTSIZE, 0);

    if(n==0 || // nothing received
        (cargo.seq - t_info.init_transfer_seq) > t_info.numpkts-1 || // packet with seq out of range
        (cargo.seq - t_info.init_transfer_seq) < ((*t_info.rcvbase)-t_info.init_transfer_seq)){ // packet processed yet
        ack = makepkt(ACK_POS, *t_info.nextseqnum, (*t_info.rcvbase)-1, receiver_window, strlen(CARGO_OK), CARGO_OK);
        if (simulateloss(1)) check(send(t_info.sockd, &ack, HEADERSIZE + ack.size, 0) , "receiver:send:ack-newbase");
        goto receive;
    }

printf("[Client:get pid:%d sockd:%d] Received cargo [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d]\n\n", me, t_info.sockd, cargo.op, cargo.seq, cargo.ack, cargo.pktleft, cargo.size);

    if( (cargo.seq - t_info.init_transfer_seq) == t_info.numpkts-1)
        *t_info.last_packet_size = cargo.size;

    check(enqueue(t_info.received_pkts, cargo), "get:enqueue:cargo");

    signal_readypkts.sem_num = 0;
    signal_readypkts.sem_op = 1;
    signal_readypkts.sem_flg = SEM_UNDO;
    check(semop(t_info.sem_readypkts, &signal_readypkts, 1), "get:semop:signal:sem_readypkts");

    goto receive; // TODO goto->while
}

/*
 *  function: sender
 *  ----------------------------
 *  Send packet to server and wait for at least an any ack
 *
 *  arg: information about transfer from put
 *
 *  return: -
 *  error: -
 */
void sender(void *arg){
    int me = (int)pthread_self();
    struct sender_info info = *((struct sender_info *)arg);
    struct sembuf sembuf_wait,sembuf_signal;
    struct pkt cargo, ack;
    int n;
    struct timespec end; //calcola endRTT

    sembuf_wait.sem_num = 0;
    sembuf_wait.sem_op = -1;
    sembuf_wait.sem_flg = SEM_UNDO;

    sembuf_signal.sem_num = 0;
    sembuf_signal.sem_op = 1;
    sembuf_signal.sem_flg = SEM_UNDO;

transmit:


    // wait if there are packets to send

    check(semop(info.sem_readypkts, &sembuf_wait, 1), "sender:semop:wait:wait_readypkts");

    // wait if there's space in the sender window

    check(semop(sem_sender_wnd, &sembuf_wait, 1), "sender:semop:wait:wait_senderwnd");

    if(pthread_mutex_lock(&info.mutex_stack) != 0){
        fprintf(stderr, "sender:pthread_mutex_lock:mutex_stack\n");
        exit(EXIT_FAILURE);
    }

    check(pop_pkt(info.stack, &cargo), "sender:pop_pkt:stack");
printf("(Client:sender tid:%d) Taken cargo packet seq:%d from stack\n\n", me, cargo.seq);

    if(pthread_mutex_unlock(&info.mutex_stack) != 0){
        fprintf(stderr, "sender:pthread_mutex_lock:mutex_stack\n");
        exit(EXIT_FAILURE);
    }

    /*if(simulateloss(0))*/ check(send(info.sockd, &cargo, HEADERSIZE+cargo.size, 0), "sender:send:cargo");
printf("[Client:sender tid:%d sockd:%d] Sended packet [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n\n", me, info.sockd, cargo.op, cargo.seq, cargo.ack, cargo.pktleft, cargo.size, (char *)cargo.data);

    //se ha inviato
    if(*(info.startRTT.seq)==-1){ //TODO put in if(info.timer)
        clock_gettime( CLOCK_REALTIME,info.startRTT.start);
printf("startRTT->start; %d  %lf per pkt; %d\n",(int)info.startRTT.start->tv_sec,(float)(1e-9)*info.startRTT.start->tv_nsec,cargo.seq-info.initialseq);
        *(info.startRTT.seq)=cargo.seq;
    }

    /*    pthread_mutex_trylock(&info.mutex_time);
        if (*(info.timer) == 0){       //avviso il padre di dormire per timeout_Interval

          //avvia sampleRTT

          check(semop(info.semTimer,&sembuf_signal,1),"THREAD: error signal semTimer");
          *(info.timer)=1;
        }
        check(pthread_mutex_unlock(&cargo.mutex_time),"THREAD: error unlock time");*/

check_ack:
    n = recv(info.sockd, &ack, MAXPKTSIZE, 0);

    // CONTINUE
    if(pthread_mutex_lock(&info.mutex_ack_counter) != 0){
        fprintf(stderr, "sender:pthread_mutex_lock:mutex_ack_counter\n");
        exit(EXIT_FAILURE);
    }

    if(n>0){
printf("sono il thread # %d e' ho ricevuto l'ack del pkt #%d \n", me, (ack.ack) - (info.initialseq) );
printf("valore di partenza in counter[%d] : %d \n", (ack.ack) - (info.initialseq), info.ack_counters[(ack.ack) - (info.initialseq)]);

        /***SAMPLE RTT****/
        if(*(info.startRTT.seq)==ack.ack){
            *(info.startRTT.seq)=-1;
            clock_gettime( CLOCK_REALTIME,&end);
printf("end %d  %lf per pkt:%d\n",(int)end.tv_sec,(float)(1e-9)*end.tv_nsec,ack.ack-info.initialseq);
            int sampleRTT = ((end.tv_sec - info.startRTT.start->tv_sec) + (1e-9)*(end.tv_nsec - info.startRTT.start->tv_nsec))*(1e6);
printf("SampleRTT: %d ns\n",sampleRTT);
            *(info.estimatedRTT)=(0.875*(*info.estimatedRTT))+(0.125*sampleRTT);
printf("new estimatedRTT: %d ns\n",*(info.estimatedRTT));
            *(info.devRTT)=(0.75*(*info.devRTT))+(0.25*(abs(sampleRTT-(*info.estimatedRTT))));
printf("new devRTT: %d ns\n",*(info.devRTT));
            *(info.timeout_Interval)=*(info.estimatedRTT)+4*(*info.devRTT);
printf("new timeout_Interval: %d ns\n",*(info.timeout_Interval));
        }

        if(ack.ack-(*(info.base))>CLIENT_SWND_SIZE || ack.ack<(*(info.base)-1)){    //ack fuori finestra
            check(pthread_mutex_unlock(&info.mutex_ack_counter),"THREAD: error unlock Ack Counters");
            memset(&ack, 0, sizeof(struct pkt));
            goto check_ack;
        }

        if(ack.ack >= *(info.base)){   //ricevo un ack nella finestra
            int k;
            for (k=*(info.base);k<=ack.ack;k++){
                //se pktlft=seq relative..da fare
                info.ack_counters[k - (info.initialseq)] = (int)info.ack_counters[k - (info.initialseq)] + 1; //sottraggo il num.seq iniziale
                (*(info.base))++; //da controllare


                check(semop(sem_sender_wnd,&sembuf_signal,1),"THREAD: error signal global at received ack ");
  printf("valore aggiornato in counter[%d] : %d \n", k - (info.initialseq), info.ack_counters[k - info.initialseq]);
            }
            if(pthread_mutex_unlock(&info.mutex_ack_counter)!=0){
                fprintf(stderr, "sender:pthread_mutex_unlock:mutex_ack_counter\n");
                exit(EXIT_FAILURE);
            }
            if(ack.ack+1 == info.initialseq+info.numpkts){
                printf("\t(Sender:pid tid:%d) Received last ack for file\n", me);
                pthread_kill(info.father_pid, SIGFINAL);
                pthread_exit(NULL);
            }
            memset(&cargo, 0, sizeof(struct pkt));
            memset(&ack, 0, sizeof(struct pkt));
            goto transmit;
        } else if(ack.ack==(*info.base)-1){   //ack duplicato
            if ((info.ack_counters[(ack.ack) - (info.initialseq)]) == 3) { // 3 duplicated acks
printf("dovrei fare una fast retransmit del pkt con #seg: %d/n", ack.ack - info.initialseq);
                (info.ack_counters[(ack.ack) - (info.initialseq)]) = 1;//(cargo.p[(rcvack.ack) - (cargo.initialseq)]) + 1;
printf("azzero il counter[%d] : %d \n", (ack.ack) - (info.initialseq), info.ack_counters[(ack.ack) - (info.initialseq)]);

                if(pthread_mutex_unlock(&info.mutex_ack_counter) != 0){
                    fprintf(stderr, "tsender:pthread_mutex_lock:mutex_ack_counter\n");
                    exit(EXIT_FAILURE);
                }

                if(pthread_mutex_lock(&info.mutex_stack) != 0){
                    fprintf(stderr, "sender:pthread_mutex_lock:mutex_stack\n");
                    exit(EXIT_FAILURE);
                }

                check(push_pkt(info.stack, cargo), "sender:pop_pkt:stack");
printf("(sender: tid%d) Locked the stack to put pkt after retransmit and pushed the packet seq:%d back into the stack\n\n", me, cargo.seq);

                if(pthread_mutex_unlock(&info.mutex_stack) != 0){
                    fprintf(stderr, "thread_sendpkt:pthread_mutex_unlock:mutex_stack\n");
                    exit(EXIT_FAILURE);
                }

                check(semop(info.sem_readypkts, &sembuf_signal, 1),"thread_sendpkt:semop:signal:sem_readypkts");
                check(semop(sem_sender_wnd, &sembuf_signal, 1),"thread_sendpkt:semop:signal:sem_readypkts");

            } else {
                (info.ack_counters[(ack.ack) - (info.initialseq)])=(int)(info.ack_counters[(ack.ack) - (info.initialseq)])+1;
                if(pthread_mutex_unlock(&info.mutex_ack_counter) != 0){
                    fprintf(stderr, "sender:pthread_mutex_unlock:mutex_ack_counter\n");
                    exit(EXIT_FAILURE);
                }
                memset(&ack, 0, sizeof(struct pkt));
                goto check_ack;
            }
        }
    }//end if(n>0)

    //se non ho ricevuto niente da rcvfrom
    if(info.ack_counters[cargo.seq-info.initialseq]>0){ //se il pkt che ho inviato è stato ackato trasmetto uno nuovo
        if(pthread_mutex_unlock(&info.mutex_ack_counter) != 0){
            fprintf(stderr, "sender:pthread_mutex_unlock:mutex_ack_counter\n");
            exit(EXIT_FAILURE);
        }
        memset(&cargo, 0, sizeof(struct pkt));
        memset(&ack, 0, sizeof(struct pkt));
        goto transmit;
    }else{ //se il mio pkt non è stato ackato continuo ad aspettare l'ack
        if(pthread_mutex_unlock(&info.mutex_ack_counter) != 0){
            fprintf(stderr, "sender:pthread_mutex_unlock:mutex_ack_counter\n");
            exit(EXIT_FAILURE);
        }
        memset(&ack, 0, sizeof(struct pkt));
        goto check_ack;
    }

};

/*
 *  function: list
 *  ----------------------------
 *  Receive and print list sent by the server
 *
 *  return: -
 *  error: -
 */
void list(void *arg){
    int me = (int)pthread_self();
    int sockd;
    struct pkt synack, cargo, ack;
    int fd;
    int n;

    sockd = request_op(&synack, SYNOP_LIST, 0, (char *)arg);
    if(sockd < 1){
printf("(Client:list tid:%d) Handshake unsuccessful, exiting operation\n\n", me);
        pthread_exit(NULL);
    }
printf("(Client:list tid:%d) Handshake successful, continuing operation\n\n", me);

recvlist:
    check_mem(memset(&cargo, 0, MAXPKTSIZE), "list:memset:cargo");
    check_mem(memset(&ack, 0, MAXPKTSIZE), "list:memset:ack");
    printf("\tWaiting for list in max %d seconds\n", CLIENT_TIMEOUT);
    n = recv(sockd, &cargo, MAXPKTSIZE, 0);

    if(n<1){
printf("[Client pid:%d sockd:%d] Sending ack to server \n",me, sockd);
        ack = makepkt(ACK_NEG, synack.seq, cargo.seq, cargo.pktleft, strlen(NOTHING_RECEIVED), NOTHING_RECEIVED);
        synack.seq++;
        if (simulateloss(1)) check(send(sockd, &ack, ack.size, 0), "list:send:ack:neg");
        goto recvlist;
    }

printf("[Client pid:%d sockd:%d] Received cargo from server [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, sockd, cargo.op, cargo.seq, cargo.ack, cargo.pktleft, cargo.size, (char *)cargo.data);

    if(cargo.size>0){
        if(pthread_mutex_lock(&mutex_list) != 0){
            fprintf(stderr, "list:pthread_mutex_lock:mutex_list\n");
            exit(EXIT_FAILURE);
        }

        fd = check(open(CLIENT_LIST_FILE, O_CREAT|O_RDWR|O_TRUNC, 0666), "list:open:fd");
        check(write(fd, cargo.data, cargo.size), "list:write:fd");

        if(pthread_mutex_unlock(&mutex_list) != 0){
            fprintf(stderr, "list:pthread_mutex_unlock:mutex_list\n");
            exit(EXIT_FAILURE);
        }

        printf("\tAvailable files on server:\n%s", cargo.data);
        close(fd);
    }else{
        printf("\tNo available file on server folder %s\n", (char *)arg);
    }

printf("[Client pid:%d sockd:%d] Sending ack to server\n", me, sockd);
    ack = makepkt(ACK_POS, synack.seq, cargo.seq, 0, strlen(CARGO_OK), CARGO_OK);
    if (simulateloss(1)) check(send(sockd, &ack, ack.size, 0), "list:send:ack");

    pthread_exit(NULL);
}

/*
 *  function: put
 *  ----------------------------
 *  Upload a file to the server
 *
 *  arg: filename to download
 *
 *  return: -
 *  error: -
 */
void put(void *arg){
    int me = (int)pthread_self();
    char *filename = (char *)arg;
    char *localpathname = check_mem(malloc((strlen(filename)+strlen(CLIENT_FOLDER))*sizeof(char)), "put:malloc:localpathname");
    // or char localpathname[strlen(CLIENT_FOLDER)+(DATASIZE)]; + check_mem(memset(&localpathname, 0, strlen(CLIENT_FOLDER)+DATASIZE), "put:memset:localpathname");
    struct pkt synack; // handshake info
    int fd; // descriptor of the file to put
    int pktseq, pktsize; // temporary sequence number and size when creating packets
    char file_strings[DATASIZE]; // temporary buffer for read from file when creating packets
    struct pkt *cargo_pkts; // memory area where file packets are stored sequentially
    struct sender_info t_info; // transfer info for sender threads
    pthread_mutex_t mutex_stack, mutex_ack_counter, mutex_time;
    struct sigaction act_lastack;
    int timer;
    int rtt = -1;
    int estimatedRTT, timeout_Interval, devRTT;
    struct timespec start;
    struct sample startRTT;

    startRTT.start=&start;
    startRTT.seq=&rtt;
    estimatedRTT=2000;
    devRTT=500;

    sprintf(localpathname, "%s%s", CLIENT_FOLDER, filename);
    t_info.numpkts = check(calculate_numpkts(localpathname), "put:calculate_numpkts:localpathname");

    /*** Handshake with server ***/
    t_info.sockd = request_op(&synack, SYNOP_PUT, t_info.numpkts, filename);
    if(t_info.sockd < 1){
printf("(Client:put tid:%d) Handshake unsuccessful, exiting operation\n\n", me);
        pthread_exit(NULL);
    }
printf("(Client:put tid:%d) Handshake successful, continuing operation\n\n", me);

    /*** Creating packets from file and setting packets stack ***/
    fd = check(open(localpathname, O_RDONLY, 0700), "put:open:localpathname");
    cargo_pkts = check_mem(malloc(t_info.numpkts*sizeof(struct pkt)), "put:malloc:cargo_pkts");
    pktseq = synack.seq+1;
    for(int p=0; p<t_info.numpkts; p++){
        pktsize = read(fd, file_strings, DATASIZE);
        cargo_pkts[p] = makepkt(CARGO, pktseq, synack.ack+1, t_info.numpkts-p, pktsize, file_strings);
        check_mem(memset(&file_strings, 0, DATASIZE), "put:memset:file_strings");
        ++pktseq;
    }
    close(fd);
printf("(Client:put tid:%d) Created %d cargo packets from %s\n\n", me, t_info.numpkts, localpathname);

    t_info.stack = check_mem(malloc(sizeof(pktstack)), "put:malloc:stack");
    for(int i=t_info.numpkts-1; i>=0; i--){
        check(push_pkt(t_info.stack, cargo_pkts[i]), "put:push_pkt:t_info.stack");
printf("(Client:put tid:%d) Pushed cargo packet #%d into the stack of packets ready to sent\n", me, i);
    }

    /*** Filling info for threads ***/
    t_info.sem_readypkts = check(semget(IPC_PRIVATE, 1, IPC_CREAT|IPC_EXCL|0666), "put:semget:sem_readypkts");
    check(semctl(t_info.sem_readypkts, t_info.numpkts, SETVAL, 0), "put:semctl:sem_readypkts");
    t_info.semTimer = check(semget(IPC_PRIVATE, 1, IPC_CREAT|IPC_EXCL|0666), "put:semget:semTimer");
    check(semctl(t_info.semTimer, 0, SETVAL, 0), "put:semctl:semTimer");
    check(pthread_mutex_init(&mutex_stack, NULL), "put:pthread_mutex_init:mutex_ack_counter");
    t_info.mutex_stack = mutex_stack;
    check(pthread_mutex_init(&mutex_ack_counter, NULL), "put:pthread_mutex_init:mutex_ack_counter");
    t_info.mutex_ack_counter = mutex_ack_counter;
    t_info.ack_counters = check_mem(calloc(t_info.numpkts, sizeof(int)), "put:calloc:ack_counters");
    t_info.base = check_mem(malloc(sizeof(int)), "put:malloc:base");
    *t_info.base = synack.seq+1;
    t_info.initialseq = synack.seq+1;
    check(pthread_mutex_init(&mutex_time, NULL), "put:pthread_mutex_init:mutex_ack_counter");
    t_info.mutex_time = mutex_time;
    t_info.timer = &timer;
    //t_info.mutex_rtt;
    t_info.devRTT = &devRTT;
    t_info.estimatedRTT = &estimatedRTT;
    t_info.startRTT = startRTT;
    t_info.timeout_Interval = &timeout_Interval;
    t_info.father_pid = pthread_self();

printf("\n(Client:put tid:%d) Started transfer of file %s, %d packets estimated\n\n", me, filename, t_info.numpkts);
    /*** Creating N threads for sending ***/
    for(int t=0; t<CLIENT_NUMTHREADS; t++){
        if(pthread_create(&ttid[t], NULL, (void *)sender, (void *)&t_info) != 0){
            fprintf(stderr, "put:pthread_create:sender%d", t);
            exit(EXIT_FAILURE);
        }
    }

    memset(&act_lastack, 0, sizeof(struct sigaction));
    act_lastack.sa_handler = &kill_handler;
    sigemptyset(&act_lastack.sa_mask);
    act_lastack.sa_flags = 0;
    check(sigaction(SIGFINAL, &act_lastack, NULL), "put:sigaction:siglastack");

    // CONTINUE

}

int main(int argc, char const *argv[]){
    pid_t me;
    pthread_t tid;
    int cmd;
    char arg[DATASIZE];
    char localpathname[strlen(CLIENT_FOLDER)+(DATASIZE)]; // put: client folder + pathname

    /*** Usage ***/
    if(argc > 3){
        fprintf(stderr, "\tQuickstart with %s, extra parameters are discarded.\n[Usage] %s [<operation-number>]\n", argv[1], argv[0]);
    }

    /*** Init ***/
    system("clear");
    me = getpid();
    printf("\tWelcome to server-simple app, client #%d\n", me);
    len = sizeof(struct sockaddr_in);
    memset((void *)&main_servaddr, 0, len);
    main_servaddr.sin_family = AF_INET;
    main_servaddr.sin_port = htons(SERVER_PORT);
    check(inet_pton(AF_INET, SERVER_ADDR, &main_servaddr.sin_addr), "main:init:inet_pton");
    sem_sender_wnd = check(semget(IPC_PRIVATE, 1, IPC_CREAT|IPC_EXCL|0666), "main:init:semget:sem_sender_wnd");
    check(semctl(sem_sender_wnd, 0, SETVAL, CLIENT_SWND_SIZE), "main:init:semctl:sem_sender_wnd");
    free_pages_rcvbuf = check_mem(malloc(CLIENT_RCVBUFSIZE * sizeof(struct index)), "main:init:malloc:free_pages_rcvbuf");
    init_index_stack(&free_pages_rcvbuf, CLIENT_RCVBUFSIZE);
    receiver_window = CLIENT_RCVBUFSIZE;
    check(pthread_mutex_init(&mutex_rcvbuf, NULL), "main:pthread_mutex_init:mutex_rcvbuf");
    check(pthread_mutex_init(&mutex_list, NULL), "main:pthread_mutex_init:mutex_list");
    if(argc == 2){
        cmd = atoi(argv[1]);
        goto quickstart;
    }

    /*** Parsing op ***/
    while (1) {
        printf("\n\tAvailable operations: 1 (list available files), 2 (get a file), 3 (put a file), 0 (exit).\n\tChoose an operation and press ENTER: ");

        if((fscanf(stdin, "%d", &cmd)) < 1){
            printf("\tInvalid operation code\n");
            fflush_stdin();
            continue;
        }

quickstart:
        /*** Operation selection ***/
        switch(cmd){
            case SYNOP_LIST:
                // TODO ask for which path to list instead of SERVER_FOLDER
                stpcpy(arg, SERVER_FOLDER); // TMP
                pthread_create(&tid, NULL, (void *)list, (void *)arg);
                pthread_join(tid, NULL); // TMP single-thread app
                break;

            case SYNOP_GET:
                printf("\tType filename to get and press ENTER: ");
                fscanf(stdin, "%s", arg);
                fflush_stdin();
                pthread_create(&tid, NULL, (void *)get, (void *)arg);
                pthread_join(tid, NULL); // TMP single-thread app
                break;

            case SYNOP_PUT:
fselect:
                printf("\tType filename to put and press ENTER: ");
                fscanf(stdin, "%s", arg);
                fflush_stdin();
                sprintf(localpathname, "%s%s", CLIENT_FOLDER, arg);
                if(calculate_numpkts(localpathname) < 1){
                    printf("\tFile not found\n");
                    goto fselect;
                }
                pthread_create(&tid, NULL, (void *)put, (void *)arg);
                pthread_join(tid, NULL); // TMP single-thread app
                break;

            case SYNOP_ABORT:
                printf("\tBye client #%d\n", me);
                exit(EXIT_SUCCESS);

            default:
                printf("\tNo operation associated with %d\n", cmd);
                break;
        } // end switch
    } // end while

    free(free_pages_rcvbuf);
    exit(EXIT_FAILURE);
}
