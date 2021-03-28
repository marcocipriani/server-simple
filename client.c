#include "common.c"
#include "config.h"

int me;
int sockd, sockputd; //aggiunta sockput per la put
int nextseqnum;
struct sockaddr_in servaddr, cliaddr;
socklen_t len;

void setsock(){
    struct timeval tout;

    sockd = check(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), "setsock:socket");
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
    check(setsockopt(sockd,SOL_SOCKET,SO_RCVTIMEO,&tout,sizeof(tout)), "setsock:setsockopt");

printf("[Client #%d] Ready to contact %s at %d.\n", me, SERVER_ADDR, SERVER_PORT);
}

void setputsock(){ //crea la socket sockputd specifica per la put
	struct timeval tout;
    
    sockputd = check(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), "setsockoutd:socket");
    tout.tv_sec = CLIENT_TIMEOUT;
	tout.tv_usec = 0;
    check(setsockopt(sockputd,SOL_SOCKET,SO_RCVTIMEO,&tout,sizeof(tout)), "setsockputd:setsockopt"); 
    
   
fprintf(stdout, "[Client #%d] Ready to trying for connection to server for put operation \n",me);
}

int put(int npkt, void *pathname) {
	int ret; // for returning values
    char *rcvbuf;
    char *sndbuf;
    struct pkt *synop, *ack, *cargo;
	
	setputsock();
	//devo fare la bind?
	nextseqnum++;
	synop = (struct pkt *)check_mem(makepkt(3, nextseqnum, 0, npkt, pathname), "setopput:makepkt"); //cmd = 3 già lo so
	
printf("[Client #%d] Sending synopput [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, 3, synop->seq, synop->ack, synop->pktleft, synop->size, (char *)synop->data);

	check(sendto(sockputd, (struct pkt *)synop, synop->size + HEADERSIZE, 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) , "setopput:sendto");
printf("[Client #%d] Waiting patiently for ack in max %d seconds...\n", me, CLIENT_TIMEOUT);
	//pacchetto synopput inviato mi metto in attesa dell'ack per la connessione
	ack = (struct pkt *)check_mem(malloc(sizeof(struct pkt *)), "setopput:malloc");
	check(recvfrom(sockputd, ack, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, &len), "setop:recvfrom");
printf("[Client #%d] Received ack from server for put operation [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, ack->op, ack->seq, ack->ack, ack->pktleft, ack->size, (char *)ack->data);

	if(strcmp(ack->data, "ok")==0){
		    return 1;
		}
	else if(strcmp(ack->data, "fullbuf")==0) {
		printf("[Client #%d] Server buffer is too small for allocating file \n",me);
		}   //can add other statuses
	return 0;
	
}


int setop(int cmd, int pktleft, void *arg){
    int ret; // for returning values
    char *rcvbuf;
    struct pkt *synop, *ack;

    nextseqnum++;
	synop = (struct pkt *)check_mem(makepkt(cmd, nextseqnum, 0, pktleft, arg), "setop:makepkt");

printf("[Client #%d] Sending synop [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, synop->op, synop->seq, synop->ack, synop->pktleft, synop->size, (char *)synop->data);
    check(sendto(sockd, (struct pkt *)synop, synop->size + HEADERSIZE, 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) , "setop:sendto");

printf("[Client #%d] Waiting patiently for ack in max %d seconds...\n", me, CLIENT_TIMEOUT);
    ack = (struct pkt *)check_mem(malloc(sizeof(struct pkt *)), "setop:malloc");
    check(recvfrom(sockd, ack, MAXTRANSUNIT, 0, (struct sockaddr *)&servaddr, &len), "setop:recvfrom");
printf("[Client #%d] Received ack from server [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", me, ack->op, ack->seq, ack->ack, ack->pktleft, ack->size, (char *)ack->data);


    if(strcmp(ack->data, "ok")==0){
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

int main(int argc, char const *argv[]) {
    
    int cmd;
    char *arg;
    off_t filelength;
	int filesize;
	int byteslastpkt;
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
    setsock();
    nextseqnum = 0;


    while (1) {  
        /* Infinite parsing input */
        printf("\nAvailable operations: 1 (list available files), 2 (get a file), 3 (put a file), 0 (exit).\nChoose an operation and press ENTER: ");
        
        fscanf(stdin, "%d", &cmd);

quickstart:
        /* Operation selection */
        switch (cmd) {
            case 1: // list
                // ask for which path to list
                if( setop(1, 0, arg) ){
printf("[Client #%d] Looking for list of default folder...\n", me);
                }
                list();
                break;
            case 2: // get
                printf("Type filename to get and press ENTER: ");
                fscanf(stdin, "%s", arg);
                if(setop(2, 0, arg)){
printf("[Client #%d] Waiting for %s...\n", me, arg);
                }
                break;
            case 3: // put
                printf("Type filename to put and press ENTER: ");
                fscanf(stdin, "%s", arg);  //salvo il pathname in arg
				//working on parsing file (path and size) 
				stat(arg, &filestat); //creo struct stat con pathname= arg e un buffer alla struttura
				filelength = filestat.st_size;
				if (filelength == 0) {
printf("File not found, check for correct pathname \n");
				exit(EXIT_FAILURE);
				} else {
					printf("[Client #%d] File selected is %s and its size is %d \n",me, arg, filelength);
				//calculate number of pkt by file size
					filesize =(int) (filelength/((off_t) DATASIZE)); 
					if (filesize <=0) {        
						filesize = 1;
					}
					else if((byteslastpkt = (int) (filelength%((off_t)DATASIZE))) != 0) { 
							filesize++;
							printf("[Client #%d] Last packet size will be %d bytes \n", me, byteslastpkt);
						} else {
							printf("File size perfectly match with pkt size! \n");
						}
					printf("[Client #%d] Number of packet that will be send for file %s is %d \n", me, arg, filesize); 
					}				
                if(put(filesize, arg)){ //ho dovuto creare put perchè mi serve una socket specifica per il client (sockputd)
printf("[Client #%d] Sending %s in the space...\n", me, arg);
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
