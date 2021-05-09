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
pthread_t put_ttid[CLIENT_NUMTHREADS + 1];

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

    wait_readypkts.sem_num = 0;
    wait_readypkts.sem_op = -1;
    wait_readypkts.sem_flg = SEM_UNDO;
    signal_writebase.sem_num = 0;
    signal_writebase.sem_op = 1;
    signal_writebase.sem_flg = SEM_UNDO;

waitforpkt:
    // wait if there are packets to be read
    check(semop(info.sem_readypkts, &wait_readypkts, 1), "receiver:semop:sem_readypkts");

    pthread_mutex_lock(&info.mutex_rcvqueue);
    if(dequeue(info.received_pkts, &cargo) == -1){
        fprintf(stderr, "Can't dequeue packet from received_pkts\n");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_lock(&info.mutex_rcvbuf);

    if(cargo.seq == PING){
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
            (*info.nextseqnum)++;
            while(info.file_cells[(*info.rcvbase)-info.init_transfer_seq] != -1){
                (*info.rcvbase)++; // increase rcvbase for every packet already processed
                if((*info.rcvbase)-info.init_transfer_seq == info.numpkts) break;
            }
            ack = makepkt(ACK_POS, *info.nextseqnum, (*info.rcvbase)-1, receiver_window, strlen(CARGO_OK), CARGO_OK);
printf("[Client:receiver tid:%d sockd:%d] Sending ack-newbase [op:%d][seq:%d][ack:%d][pktleft/rwnd:%d][size:%d][data:%s]\n\n", me, info.sockd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
            if (simulateloss(1)) check(send(info.sockd, &ack, HEADERSIZE + ack.size, 0) , "receiver:send:ack-newbase");

            // tell the thread doing writer to write base cargo packet
            check(semop(info.sem_writebase, &signal_writebase, 1), "get:semop:signal:sem_writebase");
        }else{
            (*info.nextseqnum)++;
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
    struct sembuf signal_readypkts;
    int n;

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

    // for(int t=0; t<CLIENT_NUMTHREADS; t++)
    if(pthread_create(&ttid[0], NULL, (void *)receiver, (void *)&t_info) != 0){
        fprintf(stderr, "put:pthread_create:receiver");
        exit(EXIT_FAILURE);
    }
    if(pthread_create(&ttid[1], NULL, (void *)writer, (void *)&t_info) != 0){
        fprintf(stderr, "put:pthread_create:writer");
        exit(EXIT_FAILURE);
    }

    memset(&act_lastwrite, 0, sizeof(struct sigaction));
    act_lastwrite.sa_handler = &kill_handler;
    sigemptyset(&act_lastwrite.sa_mask);
    act_lastwrite.sa_flags = 0;
    check(sigaction(SIGFINAL, &act_lastwrite, NULL), "get:sigaction:siglastwrite");

    ttid[2] = pthread_self();

receive:
    check_mem(memset((void *)&cargo, 0, sizeof(struct pkt)/*HEADERSIZE + synack.size*/), "get:memset:ack");
    n = recv(t_info.sockd, &cargo, MAXPKTSIZE, 0);

    if(n==0 || // nothing received
        (cargo.seq - t_info.init_transfer_seq) > t_info.numpkts-1 || // packet with seq out of range
        (cargo.seq - t_info.init_transfer_seq) < ((*t_info.rcvbase)-t_info.init_transfer_seq)){ // packet processed yet
        ack = makepkt(ACK_NEG, *t_info.nextseqnum, (*t_info.rcvbase)-1, receiver_window, strlen(CARGO_MISSING), CARGO_MISSING);
        if (simulateloss(1)) check(send(t_info.sockd, &ack, HEADERSIZE+ack.size, 0) , "get:send:ack-newbase");
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

    goto receive;

    free(t_info.nextseqnum);
    free(t_info.received_pkts);
    free(t_info.file_cells);
    free(t_info.rcvbase);
    free(t_info.last_packet_size);
    pthread_exit(NULL);
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
printf("sono il sender\n");
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
            check(semop(sem_sender_wnd,&sembuf_wait,1),"THREAD: error wait global");    //wait su semGlob
printf("thr:fermo al mutex_stack\n");

            if(pthread_mutex_lock(&cargo.mutex_stack) != 0) {   //lock sulla struct stack_elem
                fprintf(stderr, "sender:pthread_mutex_lock:mutex_stack\n");
                exit(EXIT_FAILURE);
            }
printf("ho preso il lock\n");
            int res = check(pop_pkt(cargo.stack,&sndpkt), "sending:pop_pkt:sndpkt");
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
            if (simulateloss(1)) check(send(cargo.sockd, &sndpkt, HEADERSIZE + sndpkt.size, 0), "thread_sendpkt:send:cargo");
printf("[Client tid:%d sockd:%d] Sended packet [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d]\n\n", me, opersd, sndpkt.op, sndpkt.seq, sndpkt.ack, sndpkt.pktleft, sndpkt.size);
        }
        else if(*cargo.rwnd == 0){
    printf("(*(*cargo.rwnd cattiva)); %d \n",(*cargo.rwnd));
    printf("(*cargo.nextseqnum) -(*cargo.base): %d\n",(*cargo.nextseqnum)-(*cargo.base) );
            if (simulateloss(1)) check(send(cargo.sockd, &emptypkt , HEADERSIZE + sndpkt.size, 0), "thread_sendpkt:send:emptypkt");
            usleep(1000);
        }
        else{
            check(semop(cargo.sem_readypkts,&sembuf_wait,1),"THREAD: error wait sem_readypkts");    //wait su semLocale=pkts_to_send
                                                                         //controllo che ci siano pkt da inviare
    printf("thr:attesa globale\n");
            check(semop(sem_sender_wnd,&sembuf_wait,1),"THREAD: error wait global");    //wait su semGlob
    printf("thr:fermo al mutex_stack\n");

            if(pthread_mutex_lock(&cargo.mutex_stack) != 0) {   //lock sulla struct stack_elem
                fprintf(stderr, "sender:pthread_mutex_lock:mutex_stack\n");
                exit(EXIT_FAILURE);
            }
            usleep(10000);

            check(pop_pkt(cargo.stack,&sndpkt), "sending:pop_pkt:sndpkt");
            if(sndpkt.seq>(*cargo.base)){
                //(*cargo.nextseqnum)++;
                (*cargo.nextseqnum)=sndpkt.seq+1;
            }

            if(pthread_mutex_unlock(&cargo.mutex_stack)!=0){   //unlock struct stack_elem
                fprintf(stderr, "sender:pthread_mutex_lock:mutex_stack\n");
                exit(EXIT_FAILURE);
            }
printf("invio pacchetto con #seq: %d\n",sndpkt.seq);
            if (simulateloss(1)) check(send(cargo.sockd, &sndpkt, HEADERSIZE + sndpkt.size, 0), "thread_sendpkt:send:cargo");
        }
    }
}

void acker(void *arg){
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

printf("(Client:acker tid%d) Actual base is %d\n", me, *cargo.base);
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
                    cargo.ack_counters[k - (cargo.initialseq)] = (int)cargo.ack_counters[k - (cargo.initialseq)] + 1; //sottraggo il num.seq iniziale
                    (*(cargo.base))++;

                    check(semop(sem_sender_wnd,&sembuf_signal,1),"THREAD: error signal global at received ack ");
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
                    //check(semop(sem_sender_wnd, &sembuf_signal, 1),"thread_sendpkt:semop:signal:sem_sender_wnd");

                    if(pthread_mutex_unlock(&cargo.mutex_stack) != 0){
                        fprintf(stderr, "thread_sendpkt:pthread_mutex_unlock:mutex_stack\n");
                        exit(EXIT_FAILURE);
                    }
    */

                    // poking the next thread waiting on transmit
                    //check(pthread_mutex_unlock(&cargo.mutex_ack_counter),"THREAD: error unlock Ack Counters");
                    //check(semop(cargo.sem_readypkts, &sembuf_signal, 1),"thread_sendpkt:semop:signal:sem_readypkts");
                    //check(semop(sem_sender_wnd, &sembuf_signal, 1),"thread_sendpkt:semop:signal:sem_readypkts");
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
    } // end while(1)
}

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
        printf("\tNothing received from server\n");
printf("[Client:list tid:%d sockd:%d] Sending negative ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, sockd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
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

        printf("\tAvailable files on server:\n%s\n", cargo.data);
        close(fd);
    }else{
        printf("\tNo available file on server folder %s\n", (char *)arg);
    }

    ack = makepkt(ACK_POS, synack.seq, cargo.seq, 0, strlen(CARGO_OK), CARGO_OK);
printf("[Client:list tid:%d sockd:%d] Sending positive ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, sockd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
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
    struct sembuf sembuf_signal;
    struct sigaction act_lastack;
    int oldbase; // base before sleep, to see if base changed
    int timer;
    int rtt = -1;
    int estimatedRTT, timeout_Interval = TIMEINTERVAL, devRTT;
    struct timespec start;
    struct sample startRTT;

    struct timespec upload;

    sembuf_signal.sem_num = 0;
    sembuf_signal.sem_op = 1;
    sembuf_signal.sem_flg = SEM_UNDO;

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
    t_info.nextseqnum = check_mem(malloc(sizeof(int)), "put:malloc:nextseqnum");
    *t_info.nextseqnum = synack.seq+1;
    t_info.sem_readypkts = check(semget(IPC_PRIVATE, 1, IPC_CREAT|IPC_EXCL|0666), "put:semget:sem_readypkts");
    check(semctl(t_info.sem_readypkts, 0, SETVAL, t_info.numpkts), "put:semctl:sem_readypkts");
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
    t_info.rwnd = &receiver_window;
    t_info.time_upload = &upload;
    t_info.array = cargo_pkts;

printf("\n(Client:put tid:%d) Started transfer of file %s, %d packets estimated\n\n", me, filename, t_info.numpkts);
    /*** Creating 1 thread for sending ***/
    if(pthread_create(&put_ttid[CLIENT_NUMTHREADS+1], NULL, (void *)sender, (void *)&t_info) != 0){
        fprintf(stderr, "put:pthread_create:sender");
        exit(EXIT_FAILURE);
    }
    /*** Creating N thread for receiving ack ***/
    for(int t=0; t<CLIENT_NUMTHREADS; t++){
        if(pthread_create(&ttid[t], NULL, (void *)acker, (void *)&t_info) != 0){
            fprintf(stderr, "put:pthread_create:acker:%d", t);
            exit(EXIT_FAILURE);
        }
    }


    memset(&act_lastack, 0, sizeof(struct sigaction));
    act_lastack.sa_handler = &kill_handler;
    sigemptyset(&act_lastack.sa_mask);
    act_lastack.sa_flags = 0;
    check(sigaction(SIGFINAL, &act_lastack, NULL), "put:sigaction:siglastack");

    while(((*t_info.base)-t_info.initialseq)<=t_info.numpkts){
        usleep(2000);
       /* oper.sem_num = 0;
        oper.sem_op = -1;
        oper.sem_flg = SEM_UNDO;

        check(semop(semTimer,&oper,1),"GET: error wait semTimer");   //WAIT su semTimer*/
        //check(pthread_mutex_lock(&mtxTime),"GET: error lock time");
        oldbase = *t_info.base;
printf("(Client:put tid%d) oldbase:%d before sleep\n", me, oldbase);
        usleep(timeout_Interval);
printf("(Client:put tid%d) oldbase:%d -> newbase:%d\n", me, oldbase, *t_info.base);
        if(oldbase==*t_info.base){ // retransmit
            if(pthread_mutex_lock(&t_info.mutex_stack) !=0){
                fprintf(stderr, "put:pthread_mutex_lock:mutex_stack\n");
            }
            check(push_pkt(t_info.stack, cargo_pkts[oldbase-t_info.initialseq]), "put:push_pkt:sendpkt[oldBase-init]");
printf("(Client:put tid%d) Pushed base packet #%d (seq:%d) to the stack\n", me, oldbase, oldbase-t_info.initialseq);
            if(pthread_mutex_unlock(&mutex_stack) !=0){
                fprintf(stderr, "put:pthread_mutex_unlock:mutex_stack\n");
            }

            check(semop(t_info.sem_readypkts, &sembuf_signal, 1),"put:semop:sem_readypkts");
            check(semop(sem_sender_wnd, &sembuf_signal, 1),"put:semop:sem_sender_wnd");
        }
        timer=0;
        //check(pthread_mutex_unlock(&mtxTime),"GET: error unlock time");

    }

    free(localpathname);
    free(cargo_pkts);
    free(t_info.stack);
    free(t_info.nextseqnum);
    free(t_info.ack_counters);
    free(t_info.base);
    pthread_exit(NULL);

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
