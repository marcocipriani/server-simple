<<<<<<< HEAD
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

int freespacebuf(int totpkt){
	size_t totpktsize;
	int res;

	totpktsize =(size_t) (totpkt*sizeof(char))*(DATASIZE*sizeof(char));
	res = sizeof(rcvbuf)-totpktsize;
	if (res >=0) {
	return 1;
	} else return 0;
}

void sendack(int idsock, int ownseq, int iack, int pktleft, char *status){ //ownseq for pkt seq of process, ack = ownseq of the other process
    struct pkt *ack;

    ack = (struct pkt *)check_mem(makepkt(4, ownseq, iack, pktleft, status), "sendack:makepkt");

    check(sendto(idsock, ack, HEADERSIZE+strlen(status), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr)) , "sendack:sendto");
printf("[Server] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", ack->op, ack->seq, ack->ack, ack->pktleft, ack->size, (char *)ack->data);
}

void list(char** res, const char* path){
    char command[DATASIZE];
    FILE* file;

    sprintf(command, "ls %s | cat > list.txt", path);
    system(command);

    file = fopen("list.txt", "r");
    fread(*res, DATASIZE, 1, file);
}

int get(int ownseq, int numpkt, char * filename){
//  int sockid;
  struct pkt* ack;
  int fd;
  //  setsock(sockid);
  ack = (struct pkt *)check_mem(malloc(sizeof(struct pkt *)), "GET-server:malloc ack");
  check(recvfrom(sockd,ack, MAXTRANSUNIT, 0, (struct sockaddr *)&cliaddr, &len), "GET-server:recvfrom ack-client");
  if(strcmp(ack->data, "ok")==0){
    printf("[SERVER] Connection established \n");
    fd = open((char *)filename,O_RDONLY,00700); //apertura file da trasferire
    if (fd == -1){
      printf("[SERVER] Problem opening file %s \n",  filename);
      exit(EXIT_FAILURE);}
  } // else other statuses
  return 0; //problem to connect with client
}

int put(struct pkt *pkt, int filesize){
	int ret; // for returning values
  int ownseq;
  char *sndbuf;
  struct pkt *cpacket;

	cpacket = pkt;
printf("[Server] Sending ACK for connection for put operation to client #%d...\n\n", cliaddr.sin_port);
}


int main(int argc, char const* argv[]) {
    char *spath = DEFAULT_PATH; // root folder for server
    struct pkt *cpacket;
    int ownseq = 0;
    char *filename;

    /* Usage */
    if(argc > 2){
        fprintf(stderr, "Path from argv[1] set, extra parameters are discarded. [Usage]: %s [<path>]\n", argv[0]);
    }

    /* Init */
    if(argc > 1) spath = (char *)argv[1];
printf("[Server] Root folder: %s\n", spath);
    setsock();
    cpacket = (struct pkt *)check_mem(malloc(sizeof(struct pkt)), "main:malloc:cpacket");
    len = sizeof(cliaddr);

    // TMP for testing list
    char *res = malloc((DATASIZE-1) * sizeof(char)); // client has to put \0 at the end
    char **resptr = &res;
    // TMP for testing ack status
    int filesize = 0, listsize = 0;

    while(1){
        /* Infinite receiving  */
        check_mem(memset((void *)&cliaddr, 0, sizeof(cliaddr)), "main:memset");
        check(recvfrom(sockd, cpacket, HEADERSIZE+DATASIZE, 0, (struct sockaddr *)&cliaddr, &len), "main:rcvfrom");
printf("[Server] Received [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", cpacket->op, cpacket->seq, cpacket->ack, cpacket->pktleft, cpacket->size, cpacket->data);

        /* Operation selection */
        switch (cpacket->op) {
            case 1: // list
                sendack(sockd, ownseq, cpacket->seq, listsize, "ok");

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
                  sendack(sockd, ownseq, cpacket->seq, filesize, "FileNotFound");
        				}
                printf("[SERVER] File selected is %s and it has generate %d pkt to transfer \n", filename, filesize);
                sendack(sockd, ownseq, cpacket->seq, filesize, "ok");
                ownseq++;
                if(get(ownseq, filesize, filename)){
                  printf("[SERVER] Sending file %s complete with success \n", filename);
                                  } else {
                  printf("[SERVER]Problem with transfer file %s to server  \n",filename);
                  				exit(EXIT_FAILURE);
                                  }
printf("My job here is done\n\n");
                    break;
            case 3: // put
            	if(freespacebuf(cpacket->pktleft)){
                	sendack(sockd, ownseq, cpacket->seq, filesize, "ok");
                	if(put(cpacket, cpacket->pktleft)) {
	printf("Transfer from Client to Server successfully complete\n\n");
					};
            	} else{
    printf("[Server] Can't handle this packet, no space for the file\n\n");
    			sendack(sockd, ownseq, cpacket->seq, 0, "fullServBuf");
            	}
                break;
            default:
printf("[Server] Can't handle this packet\n\n");
                // SEND A NACK? to protect from wrong packets
                sendack(sockd, ownseq, cpacket->seq, 0, "malformed packet");
                break;
        }
    }

    exit(EXIT_FAILURE);
}
=======
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

int freespacebuf(int totpkt){
	size_t totpktsize;
	int res;

	totpktsize =(size_t) (totpkt*sizeof(char))*(DATASIZE*sizeof(char));
	res = sizeof(rcvbuf)-totpktsize;
	if (res >=0) {
	return 1;
	} else return 0;
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
  int ownseq;
  char *sndbuf;
  struct pkt *cpacket;

	cpacket = pkt;
printf("[Server] Sending ACK for connection for put operation to client #%d...\n\n", cliaddr.sin_port);
}


int main(int argc, char const* argv[]) {
    char *spath = DEFAULT_PATH; // root folder for server
    struct pkt *cpacket;
    int ownseq = 0;

    /* Usage */
    if(argc > 2){
        fprintf(stderr, "Path from argv[1] set, extra parameters are discarded. [Usage]: %s [<path>]\n", argv[0]);
    }

    /* Init */
    if(argc > 1) spath = (char *)argv[1];
printf("[Server] Root folder: %s\n", spath);
    setsock();
    cpacket = (struct pkt *)check_mem(malloc(sizeof(struct pkt)), "main:malloc:cpacket");
    len = sizeof(cliaddr);

    // TMP for testing list
    char *res = malloc((DATASIZE-1) * sizeof(char)); // client has to put \0 at the end
    char **resptr = &res;
    // TMP for testing ack status
    int filesize = 0, listsize = 0;

    while(1){
        /* Infinite receiving  */
        check_mem(memset((void *)&cliaddr, 0, sizeof(cliaddr)), "main:memset");
        check(recvfrom(sockd, cpacket, HEADERSIZE+DATASIZE, 0, (struct sockaddr *)&cliaddr, &len), "main:rcvfrom");
printf("[Server] Received [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", cpacket->op, cpacket->seq, cpacket->ack, cpacket->pktleft, cpacket->size, cpacket->data);

        /* Operation selection */
        switch (cpacket->op) {
            case 1: // list
                sendack(sockd, ownseq, cpacket->seq, listsize, "ok");

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
                  sendack(sockd, ownseq, cpacket->seq, filesize, "FileNotFound");
        				}
                printf("[SERVER] File selected is %s and it has generate %d pkt to transfer \n", filename, filesize);
                sendack(sockd, ownseq, cpacket->seq, filesize, "ok");
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
                sendack(sockd, ownseq, cpacket->seq, filesize, "ok");
                if(put(cpacket, cpacket->pktleft)) {
	printf("Transfer from Client to Server successfully complete\n\n");
					};
            	} else{
    printf("[Server] Can't handle this packet, no space for the file\n\n");
    			sendack(sockd, ownseq, cpacket->seq, 0, "fullServBuf");
            	}
                break;
            default:
printf("[Server] Can't handle this packet\n\n");
                // SEND A NACK? to protect from wrong packets
                sendack(sockd, ownseq, cpacket->seq, 0, "malformed packet");
                break;
        }
    }

    exit(EXIT_FAILURE);
}
>>>>>>> ff7a9fce02955dd968d39823a8586415df18c480
