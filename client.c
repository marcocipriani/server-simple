#include "macro.h"
#include "common.c"

int sockd; // TODEL
struct sockaddr_in main_servaddr, cliaddr;
struct sockaddr_in cliaddr; //TODEL
socklen_t len;
int nextseqnum,initseqserver;
void **tstatus;
char rcvbuf[(DATASIZE)*CLIENT_RCVBUFSIZE];
// stack free_cells // which cells of rcvbuf are free
pthread_mutex_t write_sem; // lock for rcvbuf, free_cells and file_counter

// input: filename, numpkts, writebase_sem,
void write_onfile(){
    int last_write_made = -1; // from 0 to numpkts
    int n_bytes_to_write = DATASIZE;
    int fd;

    //fd = open();
    while(last_write_made < numpkts){
        wait(writebase_sem);
//     /lock mutex_rcvbuf
//     while(fileCounter[lastwriten]!=-1)
//       k=fileCounter[lastwriten]
//       if (lastwriten==numpkt)
//         nByteToWrite=lastpktsize
//       write(fd,rcvbuf[k],nByteToWrite)
//       push_pkt(fifo pkts, k)
//       lastwriten++
//     /unlock mutex_rcvbuf
    }
// kill(pid,LASTWRITE)
}

void receiver(struct sender_info shared_transfer_info){

start:
    wait(shared_transfer_info.stack_sem);
//     unqueue cargo from coda fifo
//     /lock mutex_rcvbuf
//     if fileCounter[(cargo.seq - initialseq)]==-1
//       POP on stack free index      //è possibile poppare piu posizioni????
//       insert on buff[free index]
//       counterFile[cargo.seq-initialseq]=free index;
//       if(cargo.seq==rcv_base)
//         while(fileCounter[rcv_base]!=-1)
//           rcv_base++;
//         sendack(rcv_base)
//         /unlock mutex_rcvbuf
//         signal(semWrite)
//       else
//         sendack(rcv_base -1)
//         /unlock mutex_rcvbuf
//       goto start
//     else
//       sendack(rcv_base -1)
//       /unlock mutex_rcvbuf
//       goto start

}

void father(){
    int rcv_base = 1;
    int *file_counter;
    struct pkt synack;
    // struct stack_node pkts_received;
    int stack_sem, writebase_sem;
    // int initialseqserver = 1;
    struct sigaction lastwrite_act;

    stack_sem = check(semget(IPC_PRIVATE, 1, IPC_CREAT|IPC_EXCL|0666));
    check(semctl(stack_sem, 1, SETVAL,  /* 0-> union semun.val; value for SETVAL */));
    writebase_sem = check(semget(IPC_PRIVATE, 1, IPC_CREAT|IPC_EXCL|0666));
    check(semctl(writebase_sem, 1, SETVAL, /* 0 -> union semun.val; value for SETVAL */));
    // setop(&synack);
    file_counter = check_mem(malloc(synack.pktlft * sizeof(int)));
    memset(file_counter, -1, synack.pktlft);

    // signal(LASTWRITE,handler)
    // lastwrite_act.sa_handler
    //                void     (*sa_sigaction)(int, siginfo_t *, void *);
    //                sigset_t   sa_mask;
    //                int        sa_flags;
    //                void     (*sa_restorer)(void);
    // sigaction(int signum, &lastwrite_act, struct sigaction *restrict oldact);

    // create child 1 doing receiver passing mega-struct
    // crete child 2 doing write_onfile passing synack.data

    //   receive:
    // n=rcvfrom(cargo)
    //   if(n==0)
    //     goto receive:
    //   push_pkt on fifo pkt al CHILD 1
    //   signal(semFifo)

}

/*
 *  function: sendack2
 *  ----------------------------
 *  Send a ack packet
 *
 *  sockd: socket descriptor used for sending
 *  op: flag for postive or negative status ack
 *  serseq: sequence number of the packet to acknowledge
 *  pktleft: // TODO ?
 *  status: verbose description of the ack
 *
 *  return: -
 *  error: -
 */
void sendack2(int sockd, int op, int serseq, int pktleft, char *status){
    pid_t me = getpid();
    struct pkt ack;

    struct sockaddr servaddr;
    getpeername(sockd, &servaddr, &len);

    nextseqnum++;
    ack = makepkt(op, nextseqnum, serseq, pktleft, strlen(status), status);

    check(sendto(sockd, &ack, HEADERSIZE+ack.size, 0, (struct sockaddr *)&servaddr, sizeof(struct sockaddr_in)), ":sendto");
printf("[Client pid:%d sockd:%d] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, sockd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
}

void *thread_sendpkt(int sockd, void *arg){
    struct elab2 *cargo; // = sender_info t_info;
    // then update common
    struct pkt sndpkt, rcvack;
    int me;

    struct sockaddr servaddr;
    getpeername(sockd, &servaddr, &len);

    cargo = (struct elab2 *)arg;
    me = (cargo->thpkt.seq) - (cargo->initialseq); // numero thread
    sndpkt = makepkt(5, cargo->thpkt.seq, 0, cargo->thpkt.pktleft, cargo->thpkt.size, cargo->thpkt.data);
printf("sono il thread # %d \n", me);
printf("valore del counter[%d] : %d \n", me, cargo->p[me]);

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

/*
 *  function: list
 *  ----------------------------
 *  Receive and print list sent by the server
 *
 *  return: -
 *  error: -
 */
void list(void *arg){
    pid_t me = getpid();
    int n;
    struct pkt listpkt;
    int fd = open("./client-files/client-list.txt", O_CREAT|O_RDWR|O_TRUNC, 0666);

    int sockd;
    struct pkt synack;
    char *folder = (char *)arg;
    struct sockaddr child_servaddr;

    sockd = setop(&synack, SYNOP_LIST, 0, folder);
    if(sockd == -1){
        printf("Operation op:%d seq:%d unsuccessful\n", synack.op, synack.seq);
        pthread_exit(NULL);
    }

    check(getpeername(sockd, &child_servaddr, &len), "list:getpeername:child_servaddr");

    // TODO
    n = recvfrom(sockd, &listpkt, MAXTRANSUNIT, 0, (struct sockaddr *)&child_servaddr, &len);
printf("[Client pid:%d sockd:%d] Received list from server [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:...]\n", me, sockd, listpkt.op, listpkt.seq, listpkt.ack, listpkt.pktleft, listpkt.size);

    if(n > 0){
        printf("Available files on server:\n");
            //buffer[n] = '\0';
            fprintf(stdout, "%s", listpkt.data);
            // lock(client-list.txt)
            write(fd, listpkt.data, listpkt.size);
            // unlock(client-list.txt)
    }else{
        printf("No available files on server\n");
        write(fd, "No available files on server\n", 30);
    }
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
    int fd;
    size_t filesize;
    int npkt,edgepkt;
    int pos,lastpktsize,rcv_base,rltv_base;
    char *localpathname;
    struct pkt cargo;

    char *filename = (char *)arg;
    int sockd;
    struct pkt synack;
    struct receiver_info;

    sockd = setop(&synack, SYNOP_GET, 0, filename);

    // parse variables to set receiver_info

    localpathname = malloc(DATASIZE * sizeof(char));
    sprintf(localpathname, "%s%s", CLIENT_FOLDER, pathname);
    printf("local %s\n",localpathname);

    npkt = pktleft;
    edgepkt=npkt; /*#pkt totali del file da ricevere SOLUZIONE: do + while!!!*/
    rcv_base=initseqserver; //per ora initseqserver è globale
    rltv_base=1;

    if(freespacebuf(npkt)){

receiver:
        while(npkt>0){
            check(recvfrom(sockd,&cargo, MAXTRANSUNIT, 0, (struct sockaddr *)&child_servaddr, &len), "GET-client:recvfrom Cargo");
printf("pacchetto ricevuto: seq %d, ack %d, pktleft %d, size %d, data %s \n", cargo.seq, cargo.ack, cargo.pktleft, cargo.size, cargo.data);
            pos=(cargo.seq - initseqserver);
printf("cargo->seq: %d \n",cargo.seq);
printf("initseqserver: %d \n",initseqserver);
printf("pos: %d \n",pos);

            if(pos>edgepkt && pos<0){   //PKT FUORI INTERVALLO
printf("numero sequenza pacchetto ricevuto fuori range \n");
                return 0;
            }
            else if((rcvbuf[pos*(DATASIZE)])==0){ // PKT NELL'INTERVALLO CORRETTO E NON ANCORA RICEVUTO
//printf("VALORE PACCHETTO %d \n",(initseqserver+edgepkt-1));
                if(cargo.seq == (initseqserver+edgepkt-1)){
                    lastpktsize = cargo.size;
                }
                memcpy(&rcvbuf[pos*(DATASIZE)],cargo.data,DATASIZE);

                if(cargo.seq == rcv_base){
                    sendack(sockd, ACK_POS, cargo.seq, cargo.pktleft/*numpkt-(cargo.seq-initseqserver)*/, "ok");
                    rcv_base++;
                }
                else{   //teoricamente se cargo.seq>rcv_base
                    sendack2(sockd, ACK_POS, rcv_base - 1,  numpkt - (rcv_base - initseqserver), "ok");
                }
printf("il pacchetto #%d e' stato scritto in pos:%d del buffer\n",cargo.seq,pos);
            }
            else{           //PKT NELL'INTERVALLO CORRETTO MA GIA' RICEVUTO
printf("pacchetto già ricevuto, posso scartarlo \n");
                sendack2(sockd, ACK_POS, rcv_base - 1, numpkt - (rcv_base - initseqserver), "ok");
                goto receiver; // il pacchetto viene scartato
            }
            npkt--;
        }

        filesize = (size_t)((DATASIZE)*(edgepkt-1))+lastpktsize; //dimensione effettiva del file
        fd = open(localpathname,O_RDWR|O_TRUNC|O_CREAT,0666);
        writen(fd,rcvbuf,filesize);
printf("il file %s e' stato correttamente scaricato\n",(char *)pathname);
		memset(rcvbuf, 0, (size_t)((DATASIZE)*(edgepkt-1))+lastpktsize );
        return 1;
    }else{
        printf("non ho trovato spazio libero nel buff \n");
        sendack2(sockd, ACK_NEG, initseqserver, pktleft, "Full_Client_Buff"); //ack negativo
        return 0;
    }
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
 // OLD int get(int sockd, int iseq, int numpkt, char *filename)
void put(void *arg){
    int i, j, k, z;
    pthread_t *tid;
    int *counter;
    int aux;
    char *dati;
    int init = iseq;

    char *filename = (char *)arg;
    int numpkts;
    struct pkt synack;
    int sockd;
    int fd;
    struct sender_info t_info;
    struct pkt *sendpkt;

    numpkts = check(calculate_numpkts(char *filename), "put:calculate_numpkts:filename");
    sockd = setop(&synack, SYNOP_PUT, numpkts, void *filename);

    fd = check(open(filename, O_RDONLY, 00700), "put:open"); // apertura file da inviare

    // alloc space for all the packets to send and copy them from file

printf("[Client] inizio trasferimento \n");
        sendpkt = malloc((numpkt) * sizeof(struct elab2)); /*Alloca la memoria per thread che eseguiranno la get */
        if(sendpkt == NULL){
printf("[Client]: ERRORE malloc sendpkt del file %s", filename);
            exit(EXIT_FAILURE);
    }

        counter = malloc((numpkt) * sizeof(int));
        if(counter == NULL){
printf("[Client]: errore malloc contatore ack \n");
            exit(EXIT_FAILURE);
        }

        tid = malloc((numpkt) * sizeof(pthread_t));
        if (tid == NULL) {
printf("[Client]: errore malloc contatore ack \n");
            exit(EXIT_FAILURE);
        }

        for (z = 0; z < numpkt; z++) {
            counter[z] = 0; // inizializza a 0 il counter
        }

        dati = (char *)malloc(DATASIZE);
        for (j = 0; j < numpkt; j++) {
            aux = readn(fd, dati, DATASIZE);
printf("aux %d \n", aux);

            sendpkt[j].thpkt = makepkt(5, iseq, 0, numpkt - j, aux, dati);
printf("[Client]: inizializzato pacchetto[%d] size %d, pktleft %d, dati %s \n", j, sendpkt[j].thpkt.size, sendpkt[j].thpkt.pktleft, sendpkt[j].thpkt.data);
            sendpkt[j].p = counter;
            sendpkt[j].initialseq = init;
            for (z = 0; z < 120; z++) {
printf("%c", sendpkt[j].thpkt.data[z]);
            }

            if(pthread_create(&tid[j], NULL, thread_sendpkt, (void *)&sendpkt[j]) != 0){
printf("[Client]:ERROR in threads creation \n");
                exit(EXIT_FAILURE);
            }
            memset(dati, 0, DATASIZE);
            iseq++;
        }

        for(k = 0; k < numpkt; k++){
printf("sono il padre e aspetto %d thread \n", numpkt - k);
            pthread_join(tid[k], tstatus);
            printf("un figlio e' morto \n");
      }
printf("tutti i thread hanno finito \n");

        // controllo che siano stati ricevuti tutti gli ACK
        for(i = 0; i < numpkt; i++){
printf("[Client] counter[%d]: %d \n", i, counter[i]);
            if(counter[i] == 0){
printf("[Client]: errore nell'invio/ricezione del pkt/ack: %d \n", i);
                return 0;
            }
        }

        return 1;
}

/*
 *  function: setop
 *  ----------------------------
 *  Check operation validity with server
 *
 *  opdata: storing synack pkt
 *  cmd: SYNOP_ABORT, SYNOP_LIST, SYNOP_GET, SYNOP_PUT
 *  pktleft: (only for put) how many packets the file is made of
 *  arg: (list) not-used (get) name of the file to get (put) name of the file to put
 *
 *  return: sockd, synack in opdata param
 *  error: 0
 */
int setop(struct pkt *opdata, int cmd, int pktleft, void *arg){
    pid_t me = getpid();
    int sockd;
    struct pkt synop, ack, synack;
    struct sockaddr_in child_servaddr;
    int n;

    sockd = setsock(main_servaddr, CLIENT_TIMEOUT, 0);

    nextseqnum++;
    synop = makepkt(cmd, nextseqnum, 0, pktleft, strlen((char *)arg), arg);
sendsynop:
printf("[Client pid:%d sockd:%d] Sending synop [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, sockd, synop.op, synop.seq, synop.ack, synop.pktleft, synop.size, (char *)synop.data);
    check(sendto(sockd, &synop, HEADERSIZE + synop.size, 0, (struct sockaddr *)&main_servaddr, sizeof(struct sockaddr_in)) , "setop:sendto:synop");

printf("[Client pid:%d sockd:%d] Waiting for ack in max %d seconds...\n", me, sockd, CLIENT_TIMEOUT);
    n = check(recvfrom(sockd, &ack, MAXTRANSUNIT, 0, (struct sockaddr *)&child_servaddr, &len), "setop:recvfrom:ack");
printf("[Client pid:%d sockd:%d] Received ack from server [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, sockd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);

    if(!n){ // nothing received
        printf("%d seconds timeout for ack expired\nRetry operation op:%d seq:%d? [Y/n] ", CLIENT_TIMEOUT, synop.op, synop.seq);
        fflush_stdin();
        if(getchar()=='n'){
            close(sockd); // sockd = 0
            return sockd;
        }else{
            goto sendsynop;
        }
    }
    if(ack.op == ACK_NEG && ack.ack == synop.seq){
        printf("Operation op:%d seq:%d not permitted\nRetry operation? [Y/n] ", synop.op, synop.seq);
        fflush_stdin();
        if(getchar()=='n'){
            close(sockd); // sockd = 0
        }else{
            goto sendsynop;
        }
    }

    if(ack.op == ACK_POS && ack.ack == synop.seq){
        printf("Operation op:%d seq:%d permitted, estimated packets: %d\nContinue? [Y/n] ", synop.op, synop.seq, ack.pktleft);
        fflush_stdin();
        if(getchar()=='n'){
            cmd = ACK_NEG;
        }else{
            cmd = ACK_POS;
            check(bind(sockd, (struct sockaddr *)&child_servaddr, sizeof(struct sockaddr)), "setop:bind:child_servaddr");
        }

        nextseqnum++;
        synack = makepkt(cmd, nextseqnum, ack.seq, ack.pktleft, strlen(synop.data), synop.data);

printf("[Client pid:%d sockd:%d] Sending synack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, sockd, synack.op, synack.seq, synack.ack, synack.pktleft, synack.size, (char *)synack.data);
        check(sendto(sockd, &synack, HEADERSIZE + synack.size, 0, (struct sockaddr *)&child_servaddr, sizeof(struct sockaddr_in)) , "setop:sendto");

        *opdata = synack;
    }

    return sockd;
}

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
    main_servaddr.sin_family = AF_INET;
    main_servaddr.sin_port = htons(SERVER_PORT);
    check(inet_pton(AF_INET, SERVER_ADDR, &main_servaddr.sin_addr), "main:init:inet_pton");
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
                pthread_create(&tid, NULL, list, (void *)arg);
                pthread_join(tid, NULL); // TMP single-thread app
                break;

            case SYNOP_GET:
                printf("Type filename to get and press ENTER: ");
                fscanf(stdin, "%s", arg);
                fflush_stdin();
                pthread_create(&tid, NULL, get, (void *)arg);
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
                pthread_create(&tid, NULL, put, (void *)arg);
                pthread_join(tid, NULL); // TMP single-thread app
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
