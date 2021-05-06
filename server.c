#include "common.c"
#include "macro.h"

int SemSnd_Wndw;
int sockd; // TODEL sockd->connsd
int connsd;
struct sockaddr_in listen_addr;
socklen_t len;
struct sockaddr_in cliaddr; // TODEL cliaddr->getpeername(sockd)
pthread_mutex_t mtxlist;
char *msg;
char rcvbuf[45000]; // buffer per la put
void **tstatus;
char *spath = SERVER_FOLDER; // root folder for server
pthread_t *ttid; // TODO sigqueue from writer to father

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
                *status = "File not available";
printf("check_validity: Invalid operation on this server\n");
                return ACK_NEG;
            }
            *status = "File available";
            break;

        case SYNOP_PUT:
            *status = "ok"; // TMP
            break;
    }
printf("check_validity: Valid operation on this server\n");
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

    //synack = check_mem(malloc(sizeof(struct pkt)), "serve_op:malloc:synack");

    /*** Create socket to perform the operation ***/
    opersd = check(setsock(opdata.cliaddr, SERVER_TIMEOUT), "serve_op:setsock:opersd");
    check(connect(opersd, (struct sockaddr *)&opdata.cliaddr, len), "serve_op:connect:cliaddr");

    /*** Create ack ***/
    status_code = check_validity(&status, &pktleft, opdata.clipacket.op, opdata.clipacket.data);
    //initseq = arc4random();
    //srand((unsigned int)time(1));
    initseq=rand()%100;

    ack = makepkt(status_code, initseq, opdata.clipacket.seq, pktleft, strlen(status), status);

    //check(sendto(opersd, &ack, HEADERSIZE + ack.size, 0, (struct sockaddr *)&opdata.cliaddr, len), "setop:sendto:ack");
    check(send(opersd, &ack, HEADERSIZE + ack.size, 0), "setop:send:ack");
printf("[Server tid:%d sockd:%d] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, opersd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);

    /*** Receive synack (response to ack) from client ***/
printf("[Server tid:%d sockd:%d] Waiting for synack in %d seconds...\n", me, opersd, SERVER_TIMEOUT);
    memset(synack, 0, sizeof(struct pkt));
    n = recv(opersd, synack, MAXTRANSUNIT, 0);

    if(n<1){
printf("No synack response from client\n");
        close(opersd);
        return -1;
    }

printf("[Server tid:%d sockd:%d] Received synack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, opersd, synack->op, synack->seq, synack->ack, synack->pktleft, synack->size, (char*)synack->data);

    if(synack->op == ACK_NEG){
printf("Client operation aborted\n");
        close(opersd);
        return -1;
    }

printf("Client operation continued\n");
    return opersd;
}

void kill_handler(){
printf("Server operation completed \n\n");
    for(int i=0;i<WSIZE;i++){
        pthread_cancel(ttid[i]);
    }
    pthread_exit(NULL);
}

//input:puntatore a pila,sem(pkts_to_send),ack_counters,base,nextseqnum,initialseq(not required if pktlft->seq relative),sockd
//timer, estimatedRTT..pid padre
void thread_sendpkt(void *arg){
    int me = (int)pthread_self();
    struct sender_info cargo;//t_info
    struct pkt sndpkt, rcvack;
    int k,n;
    int base; // unused
    union sigval retransmit_info;
    struct sembuf oper;
    cargo = *((struct sender_info *)arg);

    int opersd=cargo.sockid;
    socklen_t len;

transmit:
    memset(&sndpkt,0,sizeof(struct pkt));
    oper.sem_num = 0;
    oper.sem_op = -1;
    oper.sem_flg = SEM_UNDO;

    check(semop(cargo.semLoc,&oper,1),"THREAD: error wait semLoc");    //wait su semLocale=pkts_to_send
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
    check(send(cargo.sockid, &sndpkt, HEADERSIZE + sndpkt.size, 0), "thread_sendpkt:send:cargo");
printf("[Server tid:%d sockd:%d] Sended packet [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d]\n", me, opersd, sndpkt.op, sndpkt.seq, sndpkt.ack, sndpkt.pktleft, sndpkt.size);

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
    n=recvfrom(opersd, &rcvack, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len);

    check(pthread_mutex_lock(&cargo.mutex_ack_counter),"THREAD: error lock Ack Counters");

printf("sono il thread # %d e' ho ricevuto l'ack del pkt #%d \n", me, (rcvack.ack) - (cargo.initialseq) + 1);
printf("valore di partenza in counter[%d] : %d \n", (rcvack.ack) - (cargo.initialseq), cargo.ack_counters[(rcvack.ack) - (cargo.initialseq)]);

    if(n>0){
      if(rcvack.ack-(*(cargo.base))>WSIZE || rcvack.ack<(*(cargo.base)-1)){    //ack fuori finestra
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
          if(rcvack.ack+1 == cargo.initialseq+cargo.numpkt){//if(rcvack.pktlft==0)
  printf("GET:Ho riscontrato l'ultimo ack del file\n");
              pthread_kill(cargo.father_pid, SIGFINAL);  //finito il file-il padre dovrà mandare un ack
              pthread_exit(NULL);
          }
            goto transmit;
      }
      else if(rcvack.ack==(*cargo.base)-1){    //ack duplicato
          if ((cargo.ack_counters[(rcvack.ack) - (cargo.initialseq)]) == 3) { // 3 duplicated acks
  printf("dovrei fare una fast retransmit del pkt con #seg: %d/n", rcvack.ack);
              (cargo.ack_counters[(rcvack.ack) - (cargo.initialseq)]) = 1;//(cargo.p[(rcvack.ack) - (cargo.initialseq)]) + 1;
  printf("azzero il counter[%d] : %d \n", (rcvack.ack) - (cargo.initialseq), cargo.ack_counters[(rcvack.ack) - (cargo.initialseq)]);

              check(pthread_mutex_unlock(&cargo.mutex_ack_counter),"THREAD: error unlock Ack Counters");
              retransmit_info.sival_int = (rcvack.ack) - (cargo.initialseq)+1;
              // sigqueue(cargo.father_pid, SIGRETRANSMIT, retransmit_info);
              // signature is sigqueue(pid_t pid, int sig, const union sigval value);
          }
          else {
              (cargo.ack_counters[(rcvack.ack) - (cargo.initialseq)])=(int)(cargo.ack_counters[(rcvack.ack) - (cargo.initialseq)])+1;
              check(pthread_mutex_unlock(&cargo.mutex_ack_counter),"THREAD: error unlock Ack Counters");
              goto check_ack;
          }
      }
    }
    //se non ho ricevuto niente da rcvfrom
    if(cargo.ack_counters[sndpkt.seq-cargo.initialseq/*pktleft if setted to seq_relative*/]>0){ //se il pkt che ho inviato è stato ackato trasmetto uno nuovo
      check(pthread_mutex_unlock(&cargo.mutex_ack_counter),"THREAD: error unlock Ack Counters");
      goto transmit;
    }
    else{       //se il mio pkt non è stato ackato continuo ad aspettare l'ack
      check(pthread_mutex_unlock(&cargo.mutex_ack_counter),"THREAD: error unlock Ack Counters");
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
    char *filename, *localpathname;
    struct sembuf oper;
    struct timespec start;
    int timer;
    double estimatedRTT, timeout_Interval;
    struct sample startRTT;

    opersd = serve_op(&synack, synop);
    if(opersd < 0){
printf("Operation op:%d seq:%d unsuccessful\n", synop.clipacket.op, synop.clipacket.seq);
        pthread_exit(NULL);
    }

    int numpkt = synack.pktleft;
    int iseq = synack.ack + 1;
    int base = synack.ack + 1;
    int init = synack.ack + 1;

    startRTT.start=&start;
    startRTT.seq=-1;

    filename=(char *)malloc(synop.clipacket.size*(sizeof(char)));
    strncpy(filename,synop.clipacket.data,synop.clipacket.size);
    localpathname = (char *)malloc((DATASIZE) * sizeof(char));
    sprintf(localpathname, "%s%s", SERVER_FOLDER, filename);

    tid = pthread_self();
    pktstack stackPtr = NULL;
    timeout_Interval=TIMEINTERVAL;

    fd = check(open(localpathname, O_RDONLY, 00700), "get:open:fd");

printf("Thread %d: inizio trasferimento \n", me);
    sendpkt = malloc((numpkt) * sizeof(struct pkt)); /*Alloca la memoria per thread che eseguiranno la get */
    check_mem(sendpkt, "get:malloc:sendpkt");

    counter = malloc(numpkt*sizeof(int));
    check_mem(counter, "get:malloc:counter");
    for(z=0; z<numpkt; z++){
        counter[z] = 0; // inizializza a 0 il counter
    }

    ttid = malloc(WSIZE*sizeof(pthread_t));
    check_mem(ttid, "get:malloc:ttid");

    // TODO if numpkt < WSIZE

    filedata = (char *)malloc(DATASIZE);
    for (j = 0; j < numpkt; j++) {
        aux = readn(fd, filedata, DATASIZE);
printf("aux %d \n", aux);
printf("lunghezza dati: %lu\n", strlen((char *)filedata));

        sendpkt[j] = makepkt(CARGO, iseq, 0, numpkt - j, aux, filedata);
printf("(sendpkt[%d] SIZE %d, pktleft %d, dati %s \n", j, sendpkt[j].size, sendpkt[j].pktleft, sendpkt[j].data);
        memset(filedata, 0, DATASIZE);
        iseq++;
    }

    for (z=numpkt-1; z>=0;z--){
        push_pkt(&stackPtr, sendpkt[z]);
    }

    /*****INIZIALIZZAZIONE SEMAFORI E MUTEX**********/
    check(pthread_mutex_init(&mtxTime,NULL),"GET: errore pthread_mutex_init time");
    check(pthread_mutex_init(&mtxStack,NULL),"GET: errore pthread_mutex_init struct stack_elem");
    check(pthread_mutex_init(&mtxAck_counter,NULL),"GET: errore pthread_mutex_init Ack Counters");

    semTimer = check(semget(IPC_PRIVATE,1,IPC_CREAT|IPC_EXCL|0666),"GET: semget semTimer"); //inizializzazione SemTimer
    check(semctl(semTimer,0,SETVAL,0), "GET: semctl semTimer");

    semPkt_to_send = check(semget(IPC_PRIVATE,1,IPC_CREAT|IPC_EXCL|0666),"GET: semget semPkt_to_send"); //inizializzazione semPkt_to_send
    check(semctl(semPkt_to_send,0,SETVAL,numpkt), "GET: semctl semPkt_to_send");

    //preparo il t_info da passare ai thread
    t_info.stack = &stackPtr;
    t_info.semLoc = semPkt_to_send;
    t_info.semTimer = semTimer;
    t_info.mutex_time = mtxTime;
    t_info.mutex_stack = mtxStack;
    t_info.mutex_ack_counter = mtxAck_counter;
    t_info.ack_counters = counter;
    t_info.base = &base;
    t_info.initialseq = base;
    t_info.numpkt = numpkt;
    t_info.sockid = opersd;
    t_info.timer = &timer;
    t_info.estimatedRTT = &estimatedRTT;
    t_info.startRTT = &startRTT;
    t_info.timeout_Interval = &timeout_Interval;
    t_info.father_pid = tid;

    for(j=0;j<WSIZE;j++){
        if(pthread_create(&ttid[j], NULL, (void *)thread_sendpkt, (void *)&t_info) != 0){
printf("server:ERRORE pthread_create GET in main");
            exit(EXIT_FAILURE);
        }
    }

    memset(&act_lastack, 0, sizeof(struct sigaction));
    act_lastack.sa_handler = &kill_handler;
    sigemptyset(&act_lastack.sa_mask);
    act_lastack.sa_flags = 0;
    check(sigaction(SIGFINAL, &act_lastack, NULL), "get:sigaction:siglastack");

    //signal(SIGRETRANSMIT,push_base);//usa sigaction
    //signal(/*stop timer-base aggiornata*/);
    while((base-init)<=numpkt){
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
    check(recvfrom(opersd, &rack, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len), "PUT-server:recvfrom ack-client");
printf("[Server] Received ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", rack.op, rack.seq, rack.ack, rack.pktleft, rack.size, rack.data);

    // strcmp(rack.op, ACK_POS)
    if (strcmp(rack.data, "ok") == 0) {
    	initseqserver = rack.seq;
    	localpathname = malloc((DATASIZE) * sizeof(char));
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
 void createlist(char **res, const char *path) {

        int fdl;
        int i;
        int n_entry;
        DIR *dirp = NULL;
        struct dirent **filename;

        //PRENDO IL LOCK IL MUTEX
        check(pthread_mutex_lock(&mtxlist),"Server:pthread_mutexlist_lock");

          check_mem(dirp = opendir(path),"list nell'apertura della directory");

          printf("CONTENUTO DELLA CARTELLA [%s] \n",path);
          /*Crea un file che contiene la filelist*/

          check(fdl = open("list.txt",O_CREAT | O_RDWR | O_TRUNC,0644),"server:open server_files.txt");

          check(n_entry =scandir(path,&filename,NULL,alphasort) ,"server:scandir");

          for(i = 0; i < n_entry; i++){
              printf("%s \n",filename[i]->d_name);
              dprintf(fdl,"%s\n",filename[i]->d_name);
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
    char *res = malloc(((DATASIZE)-1)*sizeof(char)); // client has to put \0 at the end
    char **resptr = &res;
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

    createlist(resptr, spath);
    check(fdl = open("list.txt",O_RDONLY,0644),"server:open server_files.txt");

    aux = read(fdl, filedata, DATASIZE);

    listpkt = makepkt(CARGO, synack.ack + 1, 0, 0, aux, filedata);

printf("[Server] Sending list [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", listpkt.op, listpkt.seq, listpkt.ack, listpkt.pktleft, listpkt.size, (char *)listpkt.data);
    check(send(opersd, &listpkt, listpkt.size + HEADERSIZE, 0), "main:send");
printf("[Server] Waiting for ack...\n");
    n = recv(opersd, &rcvack, MAXTRANSUNIT, 0); // TMP cliaddr parsed from sender_info
    if(n<1){
printf("No ack response from client\n");
        close(opersd);
    }
    if(rcvack.ack == listpkt.seq){
printf("It's ok, i received ack about listpkt \n");
    }else{
printf("there are problems, not response ack from client \n");
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
        fprintf(stderr, "Path from argv[1] set, extra parameters are discarded. [Usage]: %s [<path>]\n", argv[0]);
    }

    /*** Init ***/
    if (argc > 1) spath = (char *)argv[1];
    me = getpid();
printf("Root folder: %s\n", spath);
    memset((void *)&listen_addr, 0, sizeof(struct sockaddr_in));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(SERVER_PORT);
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    connsd = setsock(listen_addr, 0);
    check(bind(connsd, (struct sockaddr *)&listen_addr, sizeof(struct sockaddr)), "main:bind:connsd");
    len = sizeof(struct sockaddr_in);
    ongoing_operations = 0;

    SemSnd_Wndw = check(semget(IPC_PRIVATE,1,IPC_CREAT|IPC_EXCL|0666),"MAIN: semget global");
    check(semctl(SemSnd_Wndw,0,SETVAL,WSIZE), "MAIN: semctl global");

   /*** Receiving synop (max BACKLOG) ***/
    while (1) {
        // Reset address and packet of the last operation
        check_mem(memset(&cliaddr, 0, sizeof(struct sockaddr_in)), "main:memset:cliaddr");
        check_mem(memset(&synop, 0, sizeof(struct pkt)), "main:memset:synop");
        check_mem(memset(&synop.data, 0, DATASIZE), "main:memset:synop");

printf("\n[Server pid:%d sockd:%d] Waiting for synop...\n", me, connsd);
        check(recvfrom(connsd, &synop, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len), "main:rcvfrom:synop");
printf("[Server pid:%d sockd:%d] Received synop [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, connsd, synop.op, synop.seq, synop.ack, synop.pktleft, synop.size, synop.data);

        // TODO if ongoing_operations >= BACKLOG send negative ack and goto recvfrom synop

        // Prepare op for child
        check_mem(memset(&opdata.cliaddr, 0, sizeof(struct sockaddr_in)), "main:memset:opdata.cliaddr");
        check_mem(memset(&opdata.clipacket, 0, sizeof(struct pkt)), "main:memset:opdata.clipacket");
        check_mem(memset(&opdata.clipacket.data, 0, DATASIZE), "main:memset:opdata.clipacket");

        memcpy(&opdata.cliaddr, &cliaddr, len);
printf("opdata.clipacket.data:%s %lu\n", opdata.clipacket.data, strlen(opdata.clipacket.data));
        opdata.clipacket = makepkt(synop.op, synop.seq, synop.ack, synop.pktleft, synop.size, synop.data);
printf("Creating elab [addr:%d][port:%d][op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", opdata.cliaddr.sin_addr.s_addr, opdata.cliaddr.sin_port, opdata.clipacket.op, opdata.clipacket.seq, opdata.clipacket.ack, opdata.clipacket.pktleft, opdata.clipacket.size, opdata.clipacket.data);

        /*** Operation selection ***/
        switch (opdata.clipacket.op) {

            case SYNOP_LIST:
                pthread_create(&tid, NULL, (void *)list, (void *)&opdata);
                ++ongoing_operations;
printf("Passed elab to child %d\n\n", ((int)tid));
                break;

            case SYNOP_GET:
                pthread_create(&tid, NULL, (void *)get, (void *)&opdata);
                ++ongoing_operations;
printf("Passed elab to child %d\n\n", ((int)tid));
                break;

            case SYNOP_PUT:
                //pthread_create(&tid, NULL, (void *)put, (void *)&opdata);
                ++ongoing_operations;
printf("Passed elab to child %d\n\n", ((int)tid));
                break;

            default:
printf("Can't handle this packet\n\n");
                // polite server: send ack with negative status instead of ignoring
                check_mem(memset(&ack, 0, sizeof(struct pkt)), "main:memset:ack");
                ack = makepkt(ACK_NEG, 0, opdata.clipacket.seq, opdata.clipacket.pktleft, strlen("malformed packet"), "malformed packet");
                check(sendto(connsd, &ack, HEADERSIZE + ack.size, 0, (struct sockaddr *)&opdata.cliaddr, sizeof(struct sockaddr_in)), "main:sendto:ack:malformed_packet");
printf("[Server pid:%d sockd:%d] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, connsd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
                break;
        }
    } // end while

    exit(EXIT_FAILURE);
}
