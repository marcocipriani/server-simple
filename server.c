#include "common.c"
#include "config.h"

int sockd;
int nextseqnum;
char *msg;
char rcvbuf[45000]; //buffer per la put
struct sockaddr_in cliaddr;
socklen_t len;

void setsock(){
    struct sockaddr_in servaddr;

    check(sockd = socket(AF_INET, SOCK_DGRAM, 0), "setsock:socket");

    check_mem(memset((void *)&servaddr, 0, sizeof(servaddr)), "setsock:memset");
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    check(bind(sockd, (struct sockaddr *)&servaddr, sizeof(servaddr)) , "setsock:bind");

fprintf(stdout, "[Server] Ready to accept on port %d\n\n", SERVER_PORT);
}

void sendack(int idsock, int cliseq, int pktleft, char *status){ //aggiunto idsock per specificare quale socket invia l'ack
    struct pkt *ack;

    nextseqnum++;
    ack = (struct pkt *)check_mem(makepkt(4, nextseqnum, cliseq, pktleft, status), "sendack:makepkt");

    check(sendto(idsock, ack, HEADERSIZE+strlen(status), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr)) , "sendack:sendto");
printf("[Server] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", ack->op, ack->seq, ack->ack, ack->pktleft, ack->size, (char *)ack->data);
}

int freespacebuf(int totpkt){
	size_t totpktsize;
	int res;

	totpktsize =(size_t) (totpkt*sizeof(char))*(DATASIZE*sizeof(char));
	res = sizeof(rcvbuf)-totpktsize;
	if (res >=0) {
	return 1;
	} else return 0;
}

void setrcvputsock(){ //crea la socket rcvputsockd specifica per la put
	struct timeval tout;

    rcvputsockd = check(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), "setsockoutd:socket");
    tout.tv_sec = SERVER_TIMEOUT;
	tout.tv_usec = 0;
    check(setsockopt(rcvputsockd,SOL_SOCKET,SO_RCVTIMEO,&tout,sizeof(tout)), "setrcvputsock:setsockopt");


fprintf(stdout, "[Server] Ready to accept connection from client for put operation \n");
}

void list(char** res, const char* path){
    char command[DATASIZE];
    FILE* file;

    sprintf(command, "ls %s | cat > list.txt", path);
    system(command);

    file = fopen("list.txt", "r");
    fread(*res, DATASIZE, 1, file);
}

int get(int ack, int numpkt, char * filename){
//  int sockid;
  struct pkt* ack;
  int fd;
  //  setsock(sockid);
  ack = (struct pkt *)check_mem(malloc(sizeof(struct pkt *)), "GET-server:malloc ack")
  check(recvfrom(sockd,ack, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len), "GET-server:recvfrom ack-client");
  if(strcmp(ack->data, "ok")==0){
    printf("[SERVER] Connection established \n");
    fd = open((char *)filename,O_RDONLY,00700); //apertura file da trasferire
    if (fd == -1){
      printf("[SERVER] Problem opening file %s \n",  filename);
      exit(EXIT_FAILURE);}
  } // else other statuses
  return 0;

}


int put(struct pkt *pkt, int filesize){
	int ret; // for returning values
    char *sndbuf;
    struct pkt *cpacket;

	char *status = "ok";
	cpacket = pkt;
	setrcvputsock();
	sendack(rcvputsockd, cpacket->seq, filesize, status);
printf("[Server] Sending ACK for connection for put operation to client #%d...\n\n", cliaddr.sin_port);
}


int main(int argc, char const* argv[]) {
    char *spath = DEFAULT_PATH; // root folder for server
    struct pkt *cpacket;
    char *filename;

    /* Usage */
    if(argc > 2){
        fprintf(stderr, "Path from argv[1] set, extra parameters are discarded. [Usage]: %s [<path>]\n", argv[0]);
    }

    /* Init */
    if(argc > 1) spath = (char *)argv[1];
printf("[Server] Root folder: %s\n", spath);
    nextseqnum = 0;
    setsock();
    cpacket = (struct pkt *)check_mem(malloc(sizeof(struct pkt)), "main:malloc:cpacket");
    len = sizeof(cliaddr);

    // TMP for testing list
    char *res = malloc((DATASIZE-1) * sizeof(char)); // client has to put \0 at the end
    char **resptr = &res;
    // TMP for testing ack status
    int filesize;
    int listsize = 0;

    while(1){
        /* Infinite receiving  */
        check_mem(memset((void *)&cliaddr, 0, sizeof(cliaddr)), "main:memset");
        check(recvfrom(sockd, cpacket, HEADERSIZE+DATASIZE, 0, (struct sockaddr *)&cliaddr, &len), "main:rcvfrom");
printf("[Server] Received [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", cpacket->op, cpacket->seq, cpacket->ack, cpacket->pktleft, cpacket->size, cpacket->data);

        /* Operation selection */
        switch (cpacket->op) {
            case 1: // list
                sendack(sockd, cpacket->seq, listsize, "ok");

                // TMP for testing list
                list(resptr, spath);
                check(sendto(sockd, (char *)res, strlen(res), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr)) , "main:sendto");

printf("[Server] Sending list to client #%d...\n\n", cliaddr.sin_port);
                break;
            case 2: // get
                 // calculate the size of the arg file
                strncpy(filename,cpacket->data,sizeof(cpacket->data)); /* salvo il filename del file richiesto*/
                filesize=calculate_numpkts(filename);
                if (filesize == -1) {
                  sendack(sockd, cpacket->seq, filesize, "GET:File non presente");
        				}
                printf("[SERVER] File selected is %s and it has generate %d pkt to transfer \n", filename, filesize);
                sendack(sockd, cpacket->seq, filesize, "ok");
                if(get(seq, filesize, filename)){
                  printf("[SERVER] Sending file %s complete with success \n", filename);
                                  } else {
                  printf("[SERVER]Problem with transfer file %s to server  \n",filename);
                  				exit(EXIT_FAILURE);
                                  }
printf("My job here is done\n\n");
                break;
            case 3: // put
            	if(freespacebuf(cpacket->pktleft)){
		        	if(put(cpacket, cpacket->pktleft)) {
		        	//sendack(cpacket->seq, 0, status);
	printf("My job here is done\n\n");
					};
            	} else{
    printf("[Server] Can't handle this packet, no space for the file\n\n");
    			sendack(sockd, cpacket->seq, 0, "fullbuf");
            	}
                break;
            default:
printf("[Server] Can't handle this packet\n\n");
                // SEND A NACK? to protect from wrong packets
                sendack(sockd, cpacket->seq, 0, "malformed packet");
                break;
        }
    }

    exit(EXIT_FAILURE);
}
