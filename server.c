#include "common.c"
#include "macro.h"

int sockd; // global until setop calls for setsock
struct sockaddr_in servaddr, cliaddr;
socklen_t len;
int nextseqnum;
char *msg;
char rcvbuf[45000]; // buffer per la put
void **tstatus;
char *status = "okclient";
char *spath = SERVER_FOLDER; // root folder for server

// setsock1() in common.c
void setsock2() {
    check(sockd = socket(AF_INET, SOCK_DGRAM, 0), "setsock:socket");

    check_mem(memset((void *)&servaddr, 0, sizeof(servaddr)), "setsock:memset");
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    check(bind(sockd, (struct sockaddr *)&servaddr, sizeof(servaddr)), "setsock:bind");
printf("[Server] Ready to accept on port %d\n\n", SERVER_PORT);
}

void sendack(int sockd, int op, int cliseq, int pktleft, char *status) {
    struct pkt ack;

    nextseqnum++;
    ack = makepkt(op, nextseqnum, cliseq, pktleft, strlen(status), status);

    check(sendto(sockd, &ack, HEADERSIZE + ack.size, 0, (struct sockaddr *)&cliaddr, sizeof(struct sockaddr_in)), "sendack:sendto");
printf("[Server] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
}

void sendack2(int sockd, struct pkt ack) {
    check(sendto(sockd, &ack, (HEADERSIZE) + ack.size, 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr)), "sendack:sendto");
printf("[Server] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
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

void *thread_sendpkt(void *arg) {
    struct elab2 *cargo;
    struct pkt sndpkt, rcvack;
    int me;
    char supp[DATASIZE];
    cargo = (struct elab2 *)arg;
    me = (cargo->thpkt.seq) - (cargo->initialseq); // numero thread
    sndpkt = makepkt(5, cargo->thpkt.seq, 0, cargo->thpkt.pktleft, cargo->thpkt.size, cargo->thpkt.data);
printf("sono il thread # %d \n", me);
printf("valore del counter[%d] : %d \n", me, cargo->p[me]);

    sendto(sockd, &sndpkt, HEADERSIZE + sndpkt.size, 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
check_ack:
    check(recvfrom(sockd, &rcvack, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len), "SERVER-get-thread:recvfrom ack-client");
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
printf("SONO IMPAZZITO \n");
printf("valore aggiornato in counter[%d] : %d \n", (rcvack.ack) - (cargo->initialseq), cargo->p[(rcvack.ack) - (cargo->initialseq)]);
    // unlock buffer
    goto check_ack;
    }
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

int get(int iseq, int iack, int numpkt, char *filename) { // iseq=11,iack=31,numpkt=10,filename="pluto.jpg"
    struct pkt ack;
    // struct pkt *pktget; //pkt da inviare di supporto
    struct elab2 *sendpkt;
    int fd;
    int i, j, k, z;
    pthread_t *tid;
    // void **status;
    int *counter;
    int aux;
    char *dati;
    int init = iseq;

    //  setsock();
    check(recvfrom(sockd, &ack, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len), "GET-server:recvfrom ack-client");

    if (strcmp(ack.data, "ok") == 0) { /* ho ricevuto ack positivo dal client */
printf("[SERVER] Connection established \n");
        fd = open((char *)filename, O_RDONLY, 00700); // apertura file da inviare
        if(fd == -1){ /*file non aperto correttamente*/
printf("[SERVER] Problem opening file %s \n", filename);
            exit(EXIT_FAILURE);
        }

transfer:
printf("inizio trasferimento \n");
        sendpkt = malloc((numpkt) * sizeof(struct elab2)); /*Alloca la memoria per thread che eseguiranno la get */
        if(sendpkt == NULL){
printf("server: ERRORE malloc sendpkt del file %s", filename);
            exit(EXIT_FAILURE);
      }

        counter = malloc((numpkt) * sizeof(int));
        if(counter == NULL){
printf("server: errore malloc contatore ack \n");
            exit(EXIT_FAILURE);
        }

        tid = malloc((numpkt) * sizeof(pthread_t));
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

            sendpkt[j].thpkt = makepkt(5, iseq, 0, numpkt - j, aux, dati);
printf("(sendpkt[%d] SIZE %d, pktleft %d, dati %s \n", j, sendpkt[j].thpkt.size, sendpkt[j].thpkt.pktleft, sendpkt[j].thpkt.data);
            sendpkt[j].p = counter;
            sendpkt[j].initialseq = init;
            for (z = 0; z < 120; z++) {
printf("%c", sendpkt[j].thpkt.data[z]);
            }

            if(pthread_create(&tid[j], NULL, thread_sendpkt, (void *)&sendpkt[j]) != 0){
printf("server:ERRORE pthread_create GET in main");
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
printf("counter[%d]: %d \n", i, counter[i]);
            if(counter[i] == 0){
printf("errore nell'invio/ricezione del pkt/ack: %d \n", i);
                return 0;
            }
        }

        return 1;
    }else{
printf("il client rifiuta il trasferimento del file/n");
        return 0; /* ho ricevuto ack negativo dal client */
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
    recvfrom(sockd, &synack, DATASIZE, 0, (struct sockaddr *)&cliaddr, &len);
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
    struct pkt cpacket, sack;
    struct elab epacket;
    char *spath = DEFAULT_PATH; // root folder for server
    char *filename;

    /* Usage */
    if (argc > 2) {
        fprintf(stderr, "Path from argv[1] set, extra parameters are discarded. [Usage]: %s [<path>]\n", argv[0]);
    }

    /* Init */
    if (argc > 1) spath = (char *)argv[1];
printf("Root folder: %s\n", spath);
    nextseqnum = 1;
    setsock2();
    memset((void *)&servaddr, 0, sizeof(struct sockaddr_in));
    //sockd = setsock(&servaddr, NULL, SERVER_PORT, 0, 1);
    len = sizeof(struct sockaddr_in);

    // TMP for testing list
    char *res = malloc((DATASIZE - 1) * sizeof(char)); // client has to put \0 at the end
    char **resptr = &res;
    // TMP for testing ack status
    int filesize;
    int listsize = 0;

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
                filename = malloc((cpacket.size) * sizeof(char));
                printf("filename: %s \n", filename);
                printf("filename: %s \n", cpacket.data);
                printf("lunghezza filename: %d \n", cpacket.size);
                strncpy(filename, cpacket.data, cpacket.size); // salvo il filename del file richiesto
                printf("filename copiato: %s \n", filename);
                filesize = calculate_numpkts(filename);
                if (filesize == -1) {
                    sack = makepkt(4, nextseqnum, cpacket.seq, 0, 22, "GET: File non presente");
                    sendack2(sockd, sack);
                }else{
                printf("[SERVER] File selected is %s and it has generate %d pkt to transfer \n", filename, filesize);
                sack = makepkt(4, nextseqnum, cpacket.seq, filesize, 2, "ok");
                sendack2(sockd, sack);
                if (get(nextseqnum, cpacket.seq, filesize, filename)) {
                    printf("[SERVER] Sending file %s complete with success \n", filename);
                  }else{
                      printf("[SERVER]Problem with transfer file %s to server  \n", filename);
                      exit(EXIT_FAILURE);
                  }
                }
                printf("My job here is done\n\n");
                break;

            case SYNOP_PUT: // put
            // TMP disabled by default
                sendack(sockd, ACK_NEG, cpacket.seq, 0, "generic negative status");
printf("Operation cmd:%d seq:%d status:completed unsuccessfully\n\n", epacket.clipacket.op, epacket.clipacket.seq);
                break;

            default:
printf("Can't handle this packet\n\n");
                sendack(sockd, ACK_NEG, cpacket.seq, 0, "malformed packet");
                break;
        }
    } // end while

    exit(EXIT_FAILURE);
}
