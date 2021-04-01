#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/dir.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ctype.h>

#include "config.h"

struct pkt{
    int op; // 0 synop-abort, 1 synop-list, 2 synop-get, 3 synop-put, 4 ack, 5 cargo
    int seq;
    int ack;
    int pktleft; // previously status // synop-put:totalpackets cargo:transfernumber ack-list:totalpackets ack-get:totalpackets ack-put:0
    int size;
    char data[DATASIZE]; // synop: arg, ack:operationstatus (0 ok 1 denied 2 trylater) empty for ack
};

struct elab{
    struct sockaddr_in cliaddr;
    struct pkt clipacket;
};

struct elab2{
    int initialseq;   //numero sequenza iniziale per un dato file
    int **p;          //puntatore a array di contatori (ricezione ack)
    struct pkt *thpkt;

};

struct pkt *makepkt(int op, int seq, int ack, int pktleft, void *data){
    struct pkt *packet;

    packet = (struct pkt *)malloc(sizeof(struct pkt));
    packet->op = op;
    packet->seq = seq;
    packet->ack = ack;
    packet->pktleft = pktleft;
    packet->size = strlen((char *)data); // or sizeof?
    memcpy(packet->data, data, sizeof(data));

    return packet;
}

void* check_mem(void *, const char *);
int check(int, const char *);

int calculate_numpkts(char *pathname){
    struct stat finfo;
    int numpkts = -1;

    if( stat(pathname, &finfo) == 0){
        numpkts = finfo.st_size / (DATASIZE);
        if((finfo.st_size % (DATASIZE)) != 0 || numpkts == 0) {
        ++numpkts;}
    } else printf("File %s not found, please check filename and retry \n", pathname);

    return numpkts;
}

int check_mutex(int exp, const char *msg){
    if(exp != 0){
        perror(msg);
        fprintf(stderr, "Error code %d\n", errno);
        exit(EXIT_FAILURE);
    }
    return exp;
}


int check(int exp, const char *msg){
    if(exp < 0){
        perror(msg);
        fprintf(stderr, "Error code %d\n", errno);
        exit(EXIT_FAILURE);
    }
    return exp;
}

void* check_mem(void *mem, const char *msg){
    if(mem == NULL){
        perror(msg);
        fprintf(stderr, "Error code %d\n", errno);
        exit(EXIT_FAILURE);
    }
    return mem;
}

ssize_t readn(int fd, void *vptr, size_t n) {/* Read "n" bytes from a descriptor. */
   size_t  nleft;
   ssize_t nread;
   char   *ptr;

   ptr = vptr;
   nleft = n;
   while (nleft > 0) {
       if ( (nread = read(fd, ptr, nleft)) < 0) {
           if (errno == EINTR)
               nread = 0;      /* and call read() again */
           else
               return (-1);
       } else if (nread == 0)
           break;              /* EOF */

       nleft -= nread;
       ptr += nread;
       //ALERT :if((int)nread<(int)n) break;	/* Se leggi di meno non bloccare ed esci dal ciclo*/
   }
   return (n - nleft);         /* return byte letti */
 }
