#include "config.h"
#include "common.c"

//commit prova
int me;
int sockd, sockid; //aggiunta sockput per la put
int nextseqnum;
struct sockaddr_in servaddr, cliaddr;
socklen_t len;
pthread_mutex_t wsizemutex;
int wsize = 10;

int pthread_mutex_init(pthread_mutex_t *,const pthread_mutexattr_t *);  //dichiarazioni per evitare i warning
int pthread_mutex_lock(pthread_mutex_t *);
int pthread_mutex_unlock(pthread_mutex_t *);
int mainthput(int,int,int);


void setsock(int sockid){ //futuri cambiamenti +numport
    struct timeval tout;

    sockid = check(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), "setsock:socket");
/* TODO client id = port
    memset((void *)&cliaddr, 0, sizeof(cliaddr));
    socklen_t clen = sizeof(cliaddr);
    check( (getsockname(sockd, (struct sockaddr *)&cliaddr, &clen) ), "Error getting sock name");
    me = ntohs(cliaddr.sin_port);
*/
    check_mem(memset((void *)&servaddr, 0, sizeof(servaddr)), "setsock:memset");
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    check(inet_pton(AF_INET, SERVER_ADDR, &servaddr.sin_addr), "setsock:inet_pton");

	tout.tv_sec = CLIENT_TIMEOUT;
	tout.tv_usec = 0;
    check(setsockopt(sockid,SOL_SOCKET,SO_RCVTIMEO,&tout,sizeof(tout)), "setsock:setsockopt");

printf("[Client #%d] Ready to contact %s at %d.\n", me, SERVER_ADDR, SERVER_PORT);
}

int setop(int cmd,int seq, int pktleft, void *arg){
    int ret; // for returning values
    char *rcvbuf;
    struct pkt *synop, *ack;

    //nextseqnum++;
	synop = (struct pkt *)check_mem(makepkt(cmd, seq, 0, pktleft, arg), "setop:makepkt");

printf("[Client #%d] Sending synop [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, synop->op, synop->seq, synop->ack, synop->pktleft, synop->size, (char *)synop->data);
    check(sendto(sockid, (struct pkt *)synop, synop->size + HEADERSIZE, 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) , "setop:sendto");

printf("[Client #%d] Waiting patiently for ack in max %d seconds...\n", me, CLIENT_TIMEOUT);
    ack = (struct pkt *)check_mem(malloc(sizeof(struct pkt *)), "setop:malloc");
    check(recvfrom(sockid, ack, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, &len), "setop:recvfrom");
printf("[Client #%d] Received ack from server [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, ack->op, ack->seq, ack->ack, ack->pktleft, ack->size, (char *)ack->data);


    if(strcmp(ack->data, "ok")==0){
printf("[Client #%d] Connection established \n", me);
        return 1;
    } // else other statuses
    return 0;
}

void list(){
    int n;
    char buffer[DATASIZE]; // 1024 + \0

    n = recvfrom(sockd, buffer, DATASIZE, 0, (struct sockaddr *)&servaddr, &len);
    if(n > 0){
        printf("Available files on server:\n");
            buffer[n] = '\0';
            fprintf(stdout, "%s", buffer);
    } else {
        printf("No available files on server\n");
    }
}

int put(int iseq, int npkt, void *pathname) { //rinominare in accordo con jack
	int ret; // for returning values
    int fd;
    int pktremain,pktosend;
    struct pkt *synop, *ack, *cargo;

	setsock(sockd);
	if (setop(3, iseq, npkt, pathname)){  //else goto input
		iseq++;
		//open(file) return fd
		fd = open((char *)pathname,O_RDONLY,00700); //DA TESTARE
		if (fd == -1){
			printf("[Client #%d] Problem opening file %s \n", me, pathname);
			exit(EXIT_FAILURE);}  //da risolvere, in caso di errore dove redirectiamo il codice
transfer:
		check_mutex(pthread_mutex_lock(&wsizemutex),"put:lockmutex");				//prendo il controllo di wsize
		pktremain = npkt-wsize;														//calcolo i pacchetti che mi restano
		if(pktremain <= 0 ){														//num pkt da inviare entra nella finestra, non ho pkt rimanenti
			wsize = wsize-npkt;														//aggiorno wsize
			check_mutex(pthread_mutex_unlock(&wsizemutex),"put:unlockmutex1");		//rilascio wsize
			//mainthput(fd,iseq,npkt);
			return 1;
		} else {  																	//num pkt da inviare nn entra nella finestra, avrò pkt rimasti
			pktosend = wsize;														//num pacchetti che posso inviare
			wsize = wsize-(npkt-pktremain);											//aggiorno wsize
			check_mutex(pthread_mutex_unlock(&wsizemutex),"put:unlockmutex2");		//rilascio wsize
			npkt = pktremain;														//il tot dei pkt da inviare = pkt rimasti
			//mainthput(fd,iseq,pktosend);
			goto transfer;
			exit(EXIT_FAILURE);
			}
		}
}

/*int mainthput(int fd, int seq, int npkt) { FUNZIONE TRASFERIMENTO FILE
	return 1;

}   */


int main(int argc, char const *argv[]) {

    int cmd;
    char *arg;
	int totpkt;
	int seq;
	struct stat filestat;

    /* Usage */
    if(argc > 3){
        fprintf(stderr, "Quickstart with %s, extra parameters are discarded.\n[Usage] %s [<operation-number>]\n", argv[1], argv[0]);
    }

    /* Init */
    if(argc == 2){
        cmd = atoi(argv[1]);
        goto quickstart;
    }
    arg = (char *)check_mem(malloc(DATASIZE*sizeof(char)), "main:malloc");
    me = getpid();
printf("Welcome to server-simple app, client #%d\n", me);
    setsock(sockd);
    seq = 1+rand()%99; //prendo un numero di seq iniziale random compreso tra 1 e 99
    nextseqnum = 0;

	//inizializzo semaforo per la gestione di wsize
	check_mutex(pthread_mutex_init(&wsizemutex,NULL), "main:mutex_init");


    while (1) {
        /* Infinite parsing input */
        printf("\nAvailable operations: 1 (list available files), 2 (get a file), 3 (put a file), 0 (exit).\nChoose an operation and press ENTER: ");

        fscanf(stdin, "%d", &cmd);
        //fflush(stdin);

quickstart:
        /* Operation selection */
        switch (cmd) {
            case 1: // list
                // ask for which path to list
                if( setop(1, seq, 0, arg) ){
printf("[Client #%d] Looking for list of default folder...\n", me);
                }
                list();
                break;
            case 2: // get
                printf("Type filename to get and press ENTER: ");
                fscanf(stdin, "%s", arg);
                if(setop(2, seq, 0, arg)){
printf("[Client #%d] Waiting for %s...\n", me, arg);
                }
                break;
put:
            case 3: // put
                printf("Type filename to put and press ENTER: ");
                fscanf(stdin, "%s", arg);  //salvo il pathname in arg
				//working on parsing file (path and size)
				totpkt = calculate_numpkts(arg);
				if (totpkt == -1) { goto put;
				}
				else printf("[Client #%d] File selected is %s and it has generate %d pkt to transfer \n",me, arg, totpkt);
                if(put(seq, totpkt, arg)){ //ho dovuto creare put perchè mi serve una socket specifica per il client (sockputd)
printf("[Client #%d] Sending file %s complete with success \n", me, arg);
                } else {
printf("[Client #%d]Problem with transfer file %s to server  \n", me, arg);
				exit(EXIT_FAILURE);
                }
                break;
            case 0: // exit
                fprintf(stdout, "Bye client #%d\n", me);
                exit(EXIT_SUCCESS);
            default:
                printf("No operation associated with %d\n", cmd);
                break;
        }
    }

    exit(EXIT_FAILURE);
}
