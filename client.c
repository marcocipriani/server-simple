#include "macro.h"
#include "common.c"

struct sockaddr_in main_servaddr;
struct sockaddr_in cliaddr; //TODEL
socklen_t len;
int initseqserver; // TODEL
void **tstatus;
char rcvbuf[CLIENT_RCVBUFSIZE*(DATASIZE)]; // if local
index_stack free_pages_rcvbuf;
pthread_mutex_t mutex_rcvbuf; // mutex for access to receive buffer and free rcvbuf indexes stack
// stack free_cells // which cells of rcvbuf are free
//pthread_mutex_t write_sem; // lock for rcvbuf, free_cells and file_counter
pthread_mutex_t mtxlist;
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

printf("[Client tid:%d sockd:%d] Sending synop [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, sockd, synop.op, synop.seq, synop.ack, synop.pktleft, synop.size, (char *)synop.data);
    check(sendto(sockd, &synop, HEADERSIZE + synop.size, 0, (struct sockaddr *)&main_servaddr, sizeof(struct sockaddr_in)) , "request_op:sendto:synop");

printf("[Client tid:%d sockd:%d] Waiting for ack in max %d seconds...\n", me, sockd, CLIENT_TIMEOUT);
    n = recvfrom(sockd, (struct pkt *)&ack, MAXTRANSUNIT, 0, (struct sockaddr *)&child_servaddr, &len);

    if(n==0){
        // TODO retry op
printf("No ack response from server\n");
        close(sockd);
        return -1;
    }
printf("[Client tid:%d sockd:%d] Received ack from server [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, sockd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);

    if(ack.op == ACK_NEG && ack.ack == synop.seq){
printf("Operation on server denied\n");
        *synack = makepkt(ACK_NEG, initseq, ack.seq, ack.pktleft, strlen(synop.data), synop.data);
printf("[Client tid:%d sockd:%d] Sending synack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, sockd, synack->op, synack->seq, synack->ack, synack->pktleft, synack->size, (char *)synack->data);
        check(sendto(sockd, synack, HEADERSIZE + synack->size, 0, (struct sockaddr *)&child_servaddr, len) , "request_op:send:server denied");
        close(sockd);
        return -1;
    }

    if(ack.op == ACK_POS && ack.ack == synop.seq){
        printf("Operation op:%d seq:%d permitted, estimated packets: %d\nContinue? [Y/n] ", synop.op, synop.seq, ack.pktleft);
		input = getchar();
        if(input=='n'){
            cmd = ACK_NEG;
        }else{
            cmd = ACK_POS;
            check(connect(sockd, (struct sockaddr *)&child_servaddr, len), "request_op:connect:child_servaddr");
        }

        initseq++;
        *synack = makepkt(cmd, initseq, ack.seq, ack.pktleft, strlen(synop.data), synop.data);

printf("[Client tid:%d sockd:%d] Sending synack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, sockd, synack->op, synack->seq, synack->ack, synack->pktleft, synack->size, (char *)synack->data);
        check(send(sockd, synack, HEADERSIZE + synack->size, 0) , "request_op:send:synack");
    }

    return sockd;
}

void kill_handler(){
    //struct receiver_info info = *((struct receiver_info *)arg); // for ttid

    for(int i=0;i<CLIENT_NUMTHREADS;i++){
        pthread_cancel(ttid[i]);
    }
printf("Client operation completed\n");
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
printf("Writer tid:%d starts writing on localpathname:%s\n", me, localpathname);
    fd = open(localpathname, O_RDWR|O_CREAT|O_TRUNC, 0666);

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
            info.file_cells[last_write_made]=-1;
            last_write_made = last_write_made +1;
printf("Writer tid:%d has written %d bytes from rcvbuf[%d] to %s", me, n_bytes_to_write, free_index, localpathname);
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
    //&cargo = check_mem(malloc(sizeof(struct pkt)),"RECEIVER: malloc cargo");

waitforpkt:
    // wait if there are packets to be read
    wait_readypkts.sem_num = 0;
    wait_readypkts.sem_op = -1;
    wait_readypkts.sem_flg = SEM_UNDO;
    check(semop(info.sem_readypkts, &wait_readypkts, 1), "receiver:semop:sem_readypkts");

    pthread_mutex_lock(&info.mutex_rcvqueue);
    if(dequeue(info.received_pkts, &cargo) == -1){
printf("Can't dequeue packet from received_pkts\n");
    }
    //pthread_mutex_unlock(&info.mutex_rcvqueue); TODEL if mutex_rcvbuf = mutex_rcvqueue
    usleep(1000); // TMP
    pthread_mutex_lock(&info.mutex_rcvbuf);

    if(info.file_cells[cargo.seq - info.init_transfer_seq] == -1){ // packet still not processed
        i = check(pop_index(&free_pages_rcvbuf), "receiver:pop_index:free_pages_rcvbuf");
        check_mem(memcpy(&rcvbuf[i*(DATASIZE)], &cargo.data, cargo.size), "receiver:memcpy:cargo");
        info.file_cells[cargo.seq-info.init_transfer_seq] = i;
printf("Receiver tid:%d has dequeue %d packet and has stored it in rcvbuf[%d]", me, cargo.seq-info.init_transfer_seq, i);

        if(cargo.seq == *info.rcvbase){
            (*info.nextseqnum)++; // TODO still necessary
            while(info.file_cells[(*info.rcvbase)-info.init_transfer_seq] != -1){
                (*info.rcvbase)++; // increase rcvbase for every packet already processed
                if((*info.rcvbase)-info.init_transfer_seq == info.numpkts) break;
            }
            ack = makepkt(ACK_POS, *info.nextseqnum, (*info.rcvbase)-1, cargo.pktleft, strlen(CARGO_OK), CARGO_OK);
printf("[Receiver tid:%d sockd:%d] Sending ack-newbase [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, info.sockd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
            check(send(info.sockd, &ack, HEADERSIZE + ack.size, 0) , "receiver:send:ack-newbase");

            // tell the thread doing writer to write base cargo packet
            signal_writebase.sem_num = 0;
            signal_writebase.sem_op = 1;
            signal_writebase.sem_flg = SEM_UNDO;
            check(semop(info.sem_writebase, &signal_writebase, 1), "get:semop:signal:sem_writebase");
        }else{
            (*info.nextseqnum)++; // TODO still necessary
            ack = makepkt(ACK_POS, *info.nextseqnum, (*info.rcvbase)-1, cargo.pktleft, strlen(CARGO_MISSING), CARGO_MISSING);
printf("[Receiver tid:%d sockd:%d] Sending ack-missingcargo [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, info.sockd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
            check(send(info.sockd, &ack, HEADERSIZE + ack.size, 0) , "receiver:send:ack-missingcargo");
        }
    }else{
        ack = makepkt(ACK_POS, *info.nextseqnum, (*info.rcvbase)-1, cargo.pktleft, strlen(CARGO_MISSING), CARGO_MISSING);
printf("[Receiver tid:%d sockd:%d] Sending ack-missingcargo [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, info.sockd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
        check(send(info.sockd, &ack, HEADERSIZE + ack.size, 0) , "receiver:send:ack-missingcargo");
    }

    pthread_mutex_unlock(&info.mutex_rcvbuf);
    pthread_mutex_unlock(&info.mutex_rcvqueue);

    check_mem(memset((void *)&cargo, 0, HEADERSIZE + cargo.size), "receiver:memset:cargo");
    check_mem(memset((void *)&ack, 0, HEADERSIZE + ack.size), "receiver:memset:ack");
    goto waitforpkt;

}

void father(void *arg){
    int me = (int)pthread_self();
    struct pkt synack, cargo;
    char *filename = (char *)arg; // TODO not necessary
    struct receiver_info t_info;
    pthread_mutex_t mutex_rcvqueue;
    struct sigaction act_lastwrite;
    int n;
    struct sembuf signal_readypkts;

    t_info.sockd = request_op(&synack, SYNOP_GET, 0, filename);
    if(t_info.sockd < 1){
printf("request_op:handshake unsuccessful\n");
        pthread_exit(NULL);
    }
printf("request_op:handshake successful\n");

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
    t_info.filename = filename;

    for(int i=0; i<CLIENT_NUMTHREADS; i++){
        pthread_create(&ttid[i], NULL, (void *)receiver, (void *)&t_info);
    }
    pthread_create(&ttid[CLIENT_NUMTHREADS], NULL, (void *)writer, (void *)&t_info);

    memset(&act_lastwrite, 0, sizeof(struct sigaction));
    act_lastwrite.sa_handler = &kill_handler;
    sigemptyset(&act_lastwrite.sa_mask);
    act_lastwrite.sa_flags = 0;
    check(sigaction(SIGFINAL, &act_lastwrite, NULL), "get:sigaction:siglastwrite");

    ttid[CLIENT_NUMTHREADS+1] = pthread_self();

receive:
    check_mem(memset((void *)&cargo, 0, sizeof(struct pkt)/*HEADERSIZE + synack.size*/), "get:memset:ack");
    n = recv(t_info.sockd, &cargo, MAXTRANSUNIT, 0);
printf("[Client pid:%d sockd:%d] Received cargo [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d]\n\n", me, t_info.sockd, cargo.op, cargo.seq, cargo.ack, cargo.pktleft, cargo.size);

    if(n==0 || // nothing received
        (cargo.seq - t_info.init_transfer_seq) > t_info.numpkts-1 || // packet with seq out of range
        (cargo.seq - t_info.init_transfer_seq) < ((*t_info.rcvbase)-t_info.init_transfer_seq)-1){ // packet processed yet
        goto receive;
    }
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
 *  function: get
 *  ----------------------------
 *  Download a file from the server
 *
 *  arg: filename to download
 *
 *  return: -
 *  error: -
 */
 // OLD int get(int sockd, void *pathname, int pktleft)
void get(void *arg){
    int me = (int)pthread_self();
    int fd;
    size_t filesize;
    int numpkts,edgepkt;
    int pos,lastpktsize,rcv_base,rltv_base;
    char *localpathname;
    struct pkt cargo, ack;
    char *filename = (char *)arg;
    int sockd;
    struct pkt synack;
    struct receiver_info;
    struct sockaddr_in child_servaddr;

    sockd = request_op(&synack, SYNOP_GET, 0, filename);
    if(sockd < 1){
printf("request_op:operation unsuccessful\n");
        pthread_exit(NULL);
    }

    // parse variables to set receiver_info
    //check(getpeername(sockd, (struct sockaddr *)&child_servaddr, &len), "get:getpeername:child_servaddr");

    localpathname = malloc(DATASIZE * sizeof(char));
    sprintf(localpathname, "%s%s", CLIENT_FOLDER, filename);
printf("local %s\n",localpathname);

    numpkts = synack.pktleft;
    edgepkt=numpkts; /*#pkt totali del file da ricevere SOLUZIONE: do + while!!!*/
    initseqserver = synack.ack + 1;
    rcv_base=initseqserver; //per ora initseqserver è globale
    rltv_base=1;

receiver:
    while(numpkts>0){
        check(recvfrom(sockd,&cargo, MAXTRANSUNIT, 0, (struct sockaddr *)&child_servaddr, &len), "GET-client:recvfrom Cargo");
printf("pacchetto ricevuto: seq %d, ack %d, pktleft %d, size %d, data %s \n", cargo.seq, cargo.ack, cargo.pktleft, cargo.size, cargo.data);
        pos=(cargo.seq - initseqserver);
printf("cargo->seq: %d \n",cargo.seq);
printf("initseqserver: %d \n",initseqserver);
printf("pos: %d \n",pos);

        if(pos>edgepkt && pos<0){   //PKT FUORI INTERVALLO
printf("numero sequenza pacchetto ricevuto fuori range \n");
            //return 0;
        }

        else if((rcvbuf[pos*(DATASIZE)])==0){ // PKT NELL'INTERVALLO CORRETTO E NON ANCORA RICEVUTO
//printf("VALORE PACCHETTO %d \n",(initseqserver+edgepkt-1));
            if(cargo.seq == (initseqserver+edgepkt-1)){
                lastpktsize = cargo.size;
            }
            memcpy(&rcvbuf[pos*(DATASIZE)],cargo.data,DATASIZE);

            if(cargo.seq == rcv_base){
                ack = makepkt(ACK_POS, 0, cargo.seq, cargo.pktleft, strlen(CARGO_OK), CARGO_OK);
                rcv_base++;
            }
            else{   //teoricamente se cargo.seq>rcv_base
                ack = makepkt(ACK_POS, 0, rcv_base - 1, numpkts - (rcv_base - initseqserver), strlen(CARGO_OK), CARGO_OK);
            }

            check(send(sockd, &ack, ack.size, 0), "get:send:new-cargo-ack");
printf("il pacchetto #%d e' stato scritto in pos:%d del buffer\n",cargo.seq,pos);
        }
        else{           //PKT NELL'INTERVALLO CORRETTO MA GIA' RICEVUTO
printf("pacchetto già ricevuto, posso scartarlo \n");
            ack = makepkt(ACK_POS, 0, rcv_base - 1, numpkts - (rcv_base - initseqserver), strlen(CARGO_OK), CARGO_OK);
            check(send(sockd, &ack, ack.size, 0), "get:send:duplicated-cargo-ack");
            goto receiver; // il pacchetto viene scartato
        }
        numpkts--;
    }

    filesize = (size_t)((DATASIZE)*(edgepkt-1))+lastpktsize; //dimensione effettiva del file
    fd = open(localpathname,O_RDWR|O_TRUNC|O_CREAT,0666);
    writen(fd,rcvbuf,filesize);
printf("Thread %d: il file %s e' stato correttamente scaricato\n", me, filename);
    memset(rcvbuf, 0, (size_t)((DATASIZE)*(edgepkt-1))+lastpktsize );
    //return 1;

}

/*
void thread_sendpkt(int sockd, void *arg){
    struct elab2 *cargo; // = sender_info t_info;
    // then update common
    struct pkt sndpkt, rcvack;

    int me = (int)pthread_self();
    struct sockaddr servaddr;

    cargo = (struct elab2 *)arg;
    me = (cargo->thpkt.seq) - (cargo->initialseq); // numero thread
    sndpkt = makepkt(5, cargo->thpkt.seq, 0, cargo->thpkt.pktleft, cargo->thpkt.size, cargo->thpkt.data);
printf("sono il thread # %d \n", me);

    sendto(sockd, &sndpkt, HEADERSIZE + sndpkt.size, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
check_ack:
    check(recvfrom(sockd, &rcvack, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, &len), "CLIENT-put-thread:recvfrom ack-client");
    // lock buffer
printf("sono il thread # %d e' ho ricevuto l'ack del pkt #%d \n", me, (rcvack.ack) - (cargo->initialseq) + 1);
printf("valore di partenza in counter[%d] : %d \n", (rcvack.ack) - (cargo->initialseq), cargo->p[(rcvack.ack) - (cargo->initialseq)]);

    if ((cargo->p[(rcvack.ack) - (cargo->initialseq)]) == 0) {
    cargo->p[(rcvack.ack) - (cargo->initialseq)] = (int)cargo->p[(rcvack.ack) - (cargo->initialseq)] + 1;
    printf("valore aggiornato in counter[%d] : %d \n", (rcvack.ack) - (cargo->initialseq), cargo->p[(rcvack.ack) - (cargo->initialseq)]);
    // unlock buffer

printf("sono il thread # %d e muoio \n", me);
pthread_exit(tstatus);
    } else if ((cargo->p[(rcvack.ack) - (cargo->initialseq)]) == 2) {
printf("dovrei fare una fast retransmit del pkt con #seg: %d/n", rcvack.ack);
    (cargo->p[(rcvack.ack) - (cargo->initialseq)]) = (cargo->p[(rcvack.ack) - (cargo->initialseq)]) + 1;
printf("valore aggiornato in counter[%d] : %d \n", (rcvack.ack) - (cargo->initialseq), cargo->p[(rcvack.ack) - (cargo->initialseq)]);
        // unlock buffer
        goto check_ack;
    }else{
        (cargo->p[(rcvack.ack) - (cargo->initialseq)]) = (cargo->p[(rcvack.ack) - (cargo->initialseq)]) + 1;
printf("pacchetto già ricevuto aspetto per la fast retransmit \n");
printf("valore aggiornato in counter[%d] : %d \n", (rcvack.ack) - (cargo->initialseq), cargo->p[(rcvack.ack) - (cargo->initialseq)]);
        // unlock buffer
        goto check_ack;
    }
}
*/

/*
 *  function: list
 *  ----------------------------
 *  Receive and print list sent by the server
 *
 *  return: -
 *  error: -
 */
void list(void *arg){

      char *folder = (char *)arg;
      int me = (int)pthread_self();
      int sockd;
      struct pkt synack;
      struct pkt ack;
      struct sockaddr child_servaddr;
      struct pkt listpkt;
      int fd = open("./client-files/client-list.txt", O_CREAT|O_RDWR|O_TRUNC, 0666);
      int n;

      sockd = request_op(&synack, SYNOP_LIST, 0, folder);
      if(sockd == -1){
          printf("Operation op:%d seq:%d unsuccessful\n", synack.op, synack.seq);
          pthread_exit(NULL);
      }

      check(getpeername(sockd, &child_servaddr, &len), "list:getpeername:child_servaddr");

      n = recvfrom(sockd, &listpkt, MAXTRANSUNIT, 0, (struct sockaddr *)&child_servaddr, &len);
  printf("[Client pid:%d sockd:%d] Received list from server [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:...]\n", me, sockd, listpkt.op, listpkt.seq, listpkt.ack, listpkt.pktleft, listpkt.size);

      if(n > 0){
          printf("Available files on server:\n");
              //buffer[n] = '\0';
              pthread_mutex_lock(&mtxlist);
              write(fd, listpkt.data, listpkt.size);
              pthread_mutex_unlock(&mtxlist);
              fprintf(stdout, "%s", listpkt.data); //stampa a schermo il contenuto del pacchetto
      }else{
          printf("No available files on server\n");
          write(fd, "No available files on server\n", 30);
      }
  printf("[Client pid:%d sockd:%d] Sending ack to server \n",me, sockd);
      ack = makepkt(ACK_POS, 0,listpkt.seq, listpkt.pktleft,strlen(CARGO_OK), CARGO_OK);
      check(send(sockd, &ack, ack.size, 0), "list:send:ack");

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

 // OLD int get(int sockd, int iseq, int numpkts, char *filename)
void put(void *arg){
    int i, j, k, z;
    pthread_t *tid;
    int *counter;
    int aux;
    char *dati;
    int init;// = iseq;
    int iseq; // TMP

    char *filename = (char *)arg;
    int numpkts;
    struct pkt synack;
    int sockd;
    int fd;
    struct sender_info t_info;
    struct elab2 *sendpkt;

    numpkts = check(calculate_numpkts(filename), "put:calculate_numpkts:filename");
    sockd = request_op(&synack, SYNOP_PUT, numpkts, filename);

    fd = check(open(filename, O_RDONLY, 00700), "put:open"); // apertura file da inviare

    // alloc space for all the packets to send and copy them from file

printf("[Client] inizio trasferimento \n");
        sendpkt = malloc((numpkts) * sizeof(struct elab2)); // Alloca la memoria per thread che eseguiranno la get
      //  if(sendpkt == NULL){
printf("[Client]: ERRORE malloc sendpkt del file %s", filename);
            exit(EXIT_FAILURE);
    }

        counter = malloc((numpkts) * sizeof(int));
        if(counter == NULL){
printf("[Client]: errore malloc contatore ack \n");
            exit(EXIT_FAILURE);
        }

        tid = malloc((numpkts) * sizeof(pthread_t));
        if (tid == NULL) {
printf("[Client]: errore malloc contatore ack \n");
            exit(EXIT_FAILURE);
        }

        for (z = 0; z < numpkts; z++) {
            counter[z] = 0; // inizializza a 0 il counter
        }

        dati = (char *)malloc(DATASIZE);
        for (j = 0; j < numpkts; j++) {
            aux = readn(fd, dati, DATASIZE);
printf("aux %d \n", aux);

            sendpkt[j].thpkt = makepkt(5, iseq, 0, numpkts - j, aux, dati);
printf("[Client]: inizializzato pacchetto[%d] size %d, pktleft %d, dati %s \n", j, sendpkt[j].thpkt.size, sendpkt[j].thpkt.pktleft, sendpkt[j].thpkt.data);
            sendpkt[j].p = counter;
            sendpkt[j].initialseq = init;
            for (z = 0; z < 120; z++) {
printf("%c", sendpkt[j].thpkt.data[z]);
            }

            if(pthread_create(&tid[j], NULL, (void *)thread_sendpkt, (void *)&sendpkt[j]) != 0){
printf("[Client]:ERROR in threads creation \n");
                exit(EXIT_FAILURE);
            }
            memset(dati, 0, DATASIZE);
            iseq++;
        }

        for(k = 0; k < numpkts; k++){
printf("sono il padre e aspetto %d thread \n", numpkts - k);
            pthread_join(tid[k], tstatus);
            printf("un figlio e' morto \n");
      }
printf("tutti i thread hanno finito \n");

        // controllo che siano stati ricevuti tutti gli ACK
        for(i = 0; i < numpkts; i++){
printf("[Client] counter[%d]: %d \n", i, counter[i]);
            if(counter[i] == 0){
printf("[Client]: errore nell'invio/ricezione del pkt/ack: %d \n", i);
                //return 0;
            }
        }

        //return 1;
}
 */

int main(int argc, char const *argv[]){
    pid_t me;
    pthread_t tid;
    int cmd;
    char arg[DATASIZE];
    char localpathname[strlen(CLIENT_FOLDER) + (DATASIZE)]; // put: client folder + pathname

    /*** Usage ***/
    if(argc > 3){
        fprintf(stderr, "Quickstart with %s, extra parameters are discarded.\n[Usage] %s [<operation-number>]\n", argv[1], argv[0]);
    }

    /*** Init ***/
    me = getpid();
printf("Welcome to server-simple app, client #%d\n", me);
    len = sizeof(struct sockaddr_in);
    memset((void *)&main_servaddr, 0, len);
    main_servaddr.sin_family = AF_INET;
    main_servaddr.sin_port = htons(SERVER_PORT);
    check(inet_pton(AF_INET, SERVER_ADDR, &main_servaddr.sin_addr), "main:init:inet_pton");
    free_pages_rcvbuf = check_mem(malloc(CLIENT_RCVBUFSIZE * sizeof(struct index)), "main:init:malloc:free_pages_rcvbuf");
    init_index_stack(&free_pages_rcvbuf, CLIENT_RCVBUFSIZE);
    check(pthread_mutex_init(&mutex_rcvbuf, NULL), "main:pthread_mutex_init:mutex_rcvbuf");
    check(pthread_mutex_init(&mtxlist, NULL), "main:pthread_mutex_init:mtxlist");
    if(argc == 2){
        cmd = atoi(argv[1]);
        goto quickstart;
    }

    /*** Parsing op ***/
    while (1) {
        printf("\nAvailable operations: 1 (list available files), 2 (get a file), 3 (put a file), 0 (exit).\nChoose an operation and press ENTER: ");

        if((fscanf(stdin, "%d", &cmd)) < 1){
            printf("Invalid operation code\n");
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
                printf("Type filename to get and press ENTER: ");
                fscanf(stdin, "%s", arg);
                fflush_stdin();
                pthread_create(&tid, NULL, (void *)father, (void *)arg);
                pthread_join(tid, NULL); // TMP single-thread app
                break;

            case SYNOP_PUT:
fselect:
                printf("Type filename to put and press ENTER: ");
                fscanf(stdin, "%s", arg);
                fflush_stdin();
                sprintf(localpathname, "%s%s", CLIENT_FOLDER, arg);
                if(calculate_numpkts(localpathname) < 1){
                    printf("File not found\n");
                    goto fselect;
                }
                //pthread_create(&tid, NULL, (void *)put, (void *)arg);
                //pthread_join(tid, NULL); // TMP single-thread app
                break;

            case SYNOP_ABORT:
                printf("Bye client #%d\n", me);
                exit(EXIT_SUCCESS);

            default:
                printf("No operation associated with %d\n", cmd);
                break;
        } // end switch
    } // end while

    exit(EXIT_FAILURE);
}
