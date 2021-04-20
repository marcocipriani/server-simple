#include "macro.h"
#include "common.c"

int me;
int sockd;
struct sockaddr_in servaddr, cliaddr;
socklen_t len;
int nextseqnum,initseqserver;
void **tstatus;
char rcvbuf[45000];


/*
 *  function: setop
 *  ----------------------------
 *  Check operation validity with server
 *
 *  cmd: SYNOP_ABORT, SYNOP_LIST, SYNOP_GET, SYNOP_PUT
 *  pktleft: (only for put) how many packets the file is made of
 *  arg: (list) not-used (get) name of the file to get (put) name of the file to put
 *
 *  return: quantity of packets the operation should use
 *  error: 0
 */
int setop(int cmd, int pktleft, void *arg){
    struct pkt synop, ack, synack;
    char *status = malloc((DATASIZE)*sizeof(char));
    me = getpid(); // different me for threads doing setop

    nextseqnum++;
    synop = makepkt(cmd, nextseqnum, 0, pktleft, strlen((char *)arg), arg);

printf("[Client #%d] Sending synop [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, synop.op, synop.seq, synop.ack, synop.pktleft, synop.size, (char *)synop.data);
    check(sendto(sockd, &synop, (HEADERSIZE) + synop.size, 0, (struct sockaddr *)&servaddr, sizeof(struct sockaddr_in)) , "setop:sendto");

printf("[Client #%d] Waiting patiently for ack in max %d seconds...\n", me, CLIENT_TIMEOUT);
    check(recvfrom(sockd, &ack, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, &len), "setop:recvfrom");
printf("[Client #%d] Received ack from server [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);

    initseqserver = ack.seq;

    if(ack.op == ACK_POS){
printf("Operation %d #%d permitted [estimated packets: %d]\nContinue? [Y/n] ", synop.op, synop.seq, ack.pktleft);
        fflush_stdin();
        if(getchar()=='n'){
            status = "noserver";
            cmd = ACK_NEG;
        }else{
            cmd = ACK_POS;
            status = "ok";
        }

        nextseqnum++;
        synack = makepkt(cmd, nextseqnum, 0, ack.pktleft, strlen(status), status);
printf("[Client #%d] Sending synack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, synack.op, synack.seq, synack.ack, synack.pktleft, synack.size, (char *)synack.data);
        check(sendto(sockd, &synack, synack.size + HEADERSIZE, 0, (struct sockaddr *)&servaddr, sizeof(struct sockaddr_in)) , "setop:sendto");

        if(cmd == ACK_NEG){
printf("Aborting operation...\n");
            return 0;
        }
        return ack.pktleft;
    }

printf("Operation %d #%d not permitted\n", synop.op, synop.seq);
    return 0;
}

/*
 *  function: sendack
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
void sendack(int sockd, int op, int serseq, int pktleft, char *status){
    struct pkt ack;

    nextseqnum++;
    ack = makepkt(op, nextseqnum, serseq, pktleft, strlen(status), status);

    check(sendto(sockd, &ack, HEADERSIZE+ack.size, 0, (struct sockaddr *)&servaddr, sizeof(struct sockaddr_in)), ":sendto");
printf("[Client #%d] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, ack.op, ack.seq, ack.ack, ack.pktleft, ack.size, (char *)ack.data);
}

/*
 *  function: freespacebuf
 *  ----------------------------
 *  Check if there's enough space in the receiver buffer
 *
 *  return: 1 if it's free
 *  error: 0
 */
int freespacebuf(int totpkt){
	size_t totpktsize;
	int res;

	totpktsize =(size_t) (totpkt*sizeof(char))*(DATASIZE*sizeof(char));
	res = sizeof(rcvbuf)-totpktsize;
	if (res >=0){
	       return 1;
    }else{
        return 0;
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
void list(){
    int n;
    struct pkt listpkt;
    int fd = open("./client-files/client-list.txt", O_CREAT|O_RDWR|O_TRUNC, 0666);

    n = recvfrom(sockd, &listpkt, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, &len);
printf("[Client #%d] Received list from server [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:...]\n", me, listpkt.op, listpkt.seq, listpkt.ack, listpkt.pktleft, listpkt.size);

    if(n > 0){
        printf("Available files on server:\n");
            //buffer[n] = '\0';
            fprintf(stdout, "%s", listpkt.data);
            write(fd, listpkt.data, listpkt.size);
    }else{
        printf("No available files on server\n");
        write(fd, "No available files on server\n", 30);
    }
}

void *thread_sendpkt(void *arg) {
    struct elab2 *cargo;
    struct pkt sndpkt, rcvack;
    int me;
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

int put(int iseq, int numpkt, char *filename) {
	struct elab2 *sendpkt;
    int fd;
    int i, j, k, z;
    pthread_t *tid;
    int *counter;
    int aux;
    char *dati;
    int init = iseq;

	fd = open(filename, O_RDONLY, 00700); // apertura file da inviare
	if(fd == -1){ /*file non aperto correttamente*/
printf("[SERVER] Problem opening file %s \n", filename);
		exit(EXIT_FAILURE);
	}

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


int get(int iseq, void *pathname, int pktleft){
    int fd;
    size_t filesize;
    int npkt,edgepkt;
    int pos,lastpktsize;
    char *localpathname;
    struct pkt cargo;

    localpathname = malloc(DATASIZE * sizeof(char));
    sprintf(localpathname, "%s%s", CLIENT_FOLDER, pathname);
    printf("local %s\n",localpathname);

    npkt = pktleft;
    edgepkt=npkt; /*#pkt totali del file da ricevere SOLUZIONE: do + while!!!*/

    if(freespacebuf(npkt)){

receiver:
        while(npkt>0){
            check(recvfrom(sockd,&cargo, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, &len), "GET-client:recvfrom Cargo");
printf("pacchetto ricevuto: seq %d, ack %d, pktleft %d, size %d, data %s \n", cargo.seq, cargo.ack, cargo.pktleft, cargo.size, cargo.data);
            pos=(cargo.seq - initseqserver);
printf("cargo->seq: %d \n",cargo.seq);
printf("initseqserver: %d \n",initseqserver);
printf("pos: %d \n",pos);

            if(pos>edgepkt && pos<0){
printf("numero sequenza pacchetto ricevuto fuori range \n");
                return 0;
            } else if((rcvbuf[pos*(DATASIZE)])==0){ // sono nell'intervallo corretto
            	printf("VALORE PACCHETTO %d \n",(initseqserver+edgepkt-1));
                if(cargo.seq == (initseqserver+edgepkt-1)){
                    lastpktsize = cargo.size;
                }
                memcpy(&rcvbuf[pos*(DATASIZE)],cargo.data,DATASIZE);
                sendack(sockd, ACK_POS, cargo.seq, cargo.pktleft, "ok");
printf("il pacchetto #%d e' stato scritto in pos:%d del buffer\n",cargo.seq,pos);
            }else{
            	printf("pacchetto già ricevuto, posso scartarlo \n");
                sendack(sockd, ACK_POS, cargo.seq, cargo.pktleft, "ok");
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
        sendack(sockd, ACK_NEG, initseqserver, pktleft, "Full_Client_Buff"); //ack negativo
        return 0;
    }
}

int main(int argc, char const *argv[]) {
    int cmd;
    char *arg;
    int totpkt; // ret for setop, pktleft of received ack from server
    char *localpathname = malloc(DATASIZE * sizeof(char)); // put: client folder + pathname
    int filesize; // put: filesize of file to put

    /* Usage */
    if(argc > 3){
        fprintf(stderr, "Quickstart with %s, extra parameters are discarded.\n[Usage] %s [<operation-number>]\n", argv[1], argv[0]);
    }

    /* Init */
    me = getpid();
printf("Welcome to server-simple app, client #%d\n", me);
    arg = check_mem(malloc(DATASIZE*sizeof(char)), "main:malloc:arg");
    memset((void *)&servaddr, 0, sizeof(struct sockaddr_in));
    sockd = setsock(&servaddr, SERVER_ADDR, SERVER_PORT, CLIENT_TIMEOUT, 0);
    nextseqnum = 0; // nextseqnum = 1+rand()%99;
    if(argc == 2){
        cmd = atoi(argv[1]);
        goto quickstart;
    }

    while (1) {
        /* Infinite parsing input */
        printf("\nAvailable operations: 1 (list available files), 2 (get a file), 3 (put a file), 0 (exit).\nChoose an operation and press ENTER: ");

        if((fscanf(stdin, "%d", &cmd)) < 1){
            printf("Invalid operation code\n");
            fflush_stdin();
            continue;
        }

quickstart:
        /* Operation selection */
        switch(cmd){
            case SYNOP_LIST: // list
                // TODO ask for which path to list instead of SERVER_FOLDER
                arg = SERVER_FOLDER;
                if((totpkt = setop(SYNOP_LIST, 0, arg)) > 0){
                    list();
                }
                break;

            case SYNOP_GET: // get
                printf("Type filename to get and press ENTER: ");
                fscanf(stdin, "%s", arg);
                fflush_stdin();
                if((totpkt = setop(SYNOP_GET, 0, arg)) > 0){
                    get(nextseqnum, arg, totpkt);
                }

                break;

            case SYNOP_PUT: // put
fselect:
                printf("Type filename to put and press ENTER: ");
                fscanf(stdin, "%s", arg);
                localpathname = malloc(DATASIZE * sizeof(char));
        	    sprintf(localpathname, "%s%s", CLIENT_FOLDER, arg);
                if((filesize = calculate_numpkts(localpathname)) < 1){
                    printf("File not found\n");
                    fflush_stdin();
                    goto fselect;
                }
                if((totpkt = setop(SYNOP_PUT, filesize, arg)) > 0){
                	if (put(nextseqnum, filesize, localpathname)) { //essendo globale nextseqnum viene aggiornato nella setop
printf("[Client] Sending file %s complete with success \n", arg);
                  	} else{
printf("[CLient]Problem with transfer file %s to server  \n", arg);
                      exit(EXIT_FAILURE);
                  }
                } else {
                	printf("[Client] Invalid ack received from server, try to contact it again \n");
                	goto fselect;
                }
                break;

            case SYNOP_ABORT: // exit
                printf("Bye client #%d\n", me);
                exit(EXIT_SUCCESS);

            default:
                printf("No operation associated with %d\n", cmd);
                break;
        } // end switch
    } // end while

    exit(EXIT_FAILURE);
}
