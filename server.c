#include "common.c"
#include "macro.h"

int connsd;
struct sockaddr_in servaddr;
socklen_t len;
struct sockaddr_in cliaddr; // TODEL
int nextseqnum, initseqserver;
char *msg;
char rcvbuf[45000]; // buffer per la put
void **tstatus;
char *status = "okclient";
char *spath = SERVER_FOLDER; // root folder for server

void sendack2(int opersd, int op, int cliseq, int pktleft, char *status) {
    struct pkt ack;

	check_mem(memset(ack.data, 0, ((DATASIZE) * sizeof(char))), "main:memset:cpacket");
    nextseqnum++;
    ack = makepkt(op, nextseqnum, cliseq, pktleft, strlen(status), status);

    check(sendto(opersd, &ack, HEADERSIZE + ack.size, 0, (struct sockaddr *)&cliaddr, sizeof(struct sockaddr_in)), "sendack2:sendto");
printf("[Server] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);

}

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

void *thread_sendpkt(void *arg) {
    struct elab2 *cargo;
    struct pkt sndpkt, rcvack;
    int me;
    int opersd; // TMP passed by arg
    cargo = (struct elab2 *)arg;
    me = (cargo->thpkt.seq) - (cargo->initialseq); // numero thread
    sndpkt = makepkt(5, cargo->thpkt.seq, 0, cargo->thpkt.pktleft, cargo->thpkt.size, cargo->thpkt.data);
printf("sono il thread # %d \n", me);
printf("valore del counter[%d] : %d \n", me, cargo->p[me]);

    sendto(opersd, &sndpkt, HEADERSIZE + sndpkt.size, 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
check_ack:
    check(recvfrom(opersd, &rcvack, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len), "SERVER-get-thread:recvfrom ack-client");
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

int get(int iseq, int numpkt, char *filename) { // iseq=11,iack=31,numpkt=10,filename="pluto.jpg"
    struct pkt ack;
    struct elab2 *sendpkt;
    int fd;
    int i, j, k, z;
    pthread_t *tid;
    int *counter;
    int aux;
    char *dati;
    int init = iseq;
    int opersd; // TMP returned from setop


	check_mem(memset(ack.data, 0, ((DATASIZE) * sizeof(char))), "GET-server:memset:ack-client");
    check(recvfrom(opersd, &ack, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len), "GET-server:recvfrom ack-client");
printf("[Server] Received ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, ack.data);
    if (strcmp(ack.data, "ok") == 0) { /* ho ricevuto ack positivo dal client */
printf("[SERVER] Connection established \n");


        fd = open(filename, O_RDONLY, 00700); // apertura file da inviare
        if(fd == -1){ /*file non aperto correttamente*/
printf("[SERVER] Problem opening file %s \n", filename);
            exit(EXIT_FAILURE);
        }

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

/*
 *  function: setop
 *  ----------------------------
 *  Serve client request
 *
 *  opdata: client packet and client adddress
 *
 *  return: socket id
 *  error: -1
 */
int setop(struct elab opdata) {
    pid_t me = getpid();
    struct pkt ack, synack;
    int opersd;
    int pktleft;
    char *status;

    opersd = setsock(opdata.cliaddr, CLIENT_TIMEOUT, 1);

    // TODO check operation validity and calculate pktleft and status
    pktleft = 0; // TMP
    if(pktleft){
        status = "ok client"; // TMP
        nextseqnum++;
        ack = makepkt(ACK_POS, nextseqnum, opdata.clipacket.seq, pktleft, strlen((char *)status), status);
    }else{
        status = "sorry client"; // TMP
        ack = makepkt(ACK_NEG, nextseqnum, opdata.clipacket.seq, pktleft, strlen((char *)status), status);
    }

    check(sendto(opersd, &ack, HEADERSIZE + ack.size, 0, (struct sockaddr *)&opdata.cliaddr, sizeof(struct sockaddr_in)), "setop:sendto:ack");
printf("[Server pid:%d sockd:%d] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, opersd, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);

printf("[Server pid:%d sockd:%d] Waiting for synack in %d seconds...\n", SERVER_TIMEOUT, me, opersd);
    recvfrom(opersd, &synack, DATASIZE, 0, (struct sockaddr *)&opdata.cliaddr, &len);
printf("[Server pid:%d sockd:%d] Received [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, opersd, synack.op, synack.seq, synack.ack, synack.pktleft, synack.size, synack.data);

    if(synack.op == ACK_NEG){
printf("Client operation aborted\n");
        return -1;
    }

printf("Client operation continued\n");
    return opersd;
}

int main(int argc, char const *argv[]) {
    struct pkt synop, ack; // ack only when rejecting packets with bad op code
    struct sockaddr_in cliaddr;
    struct elab opdata;
    pid_t me;
    pthread_t tid;
    int ongoing_operations;
    char *spath = DEFAULT_PATH; // root folder for server
    char *filename, *localpathname;

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

    /*** Receiving synop (max BACKLOG) ***/
    while (1) {
        // Reset address and packet of the last operation
        check_mem(memset(&cliaddr, 0, sizeof(struct sockaddr_in)), "main:memset:cliaddr");
        check_mem(memset(&synop, 0, sizeof(struct pkt)), "main:memset:synop");

printf("[Server pid:%d sockd:%d] Waiting for synop...\n", me, connsd);
        check(recvfrom(connsd, &synop, HEADERSIZE + DATASIZE, 0, (struct sockaddr *)&cliaddr, &len), "main:rcvfrom:synop");
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
