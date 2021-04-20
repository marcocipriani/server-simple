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
#include <pthread.h>
#include <ctype.h>
#include <termios.h>
//#include <sys/mman.h>

#include "macro.h"

struct pkt{
    int op; // op codes in macro.h
    int seq;
    int ack;
    int pktleft;
        // client synop-abort:notset synop-list:notset synop-get:notset synop-put:totalpackets cargo:relativepktnumber ack-list:notset ack-get:notset
        // server ack-list:totalpackets ack-get:totalpackets ack-put:notset cargo:relativepktnumber
    int size;
    char data[DATASIZE]; // synop: arg, ack:operationstatus (0 ok 1 denied 2 trylater) empty for ack
};

// TMP
struct elab{
    struct sockaddr_in cliaddr;
    struct pkt clipacket;
};

struct elab2{
    int initialseq;   //numero sequenza iniziale per un dato file
    int *p;          //puntatore a array di contatori (ricezione ack)
    struct pkt thpkt;
};

/*
 *  function: makepkt
 *  ----------------------------
 *  Create a packet to send
 *
 *  op: SYNOP_ABORT, SYNOP_LIST, SYNOP_GET, SYNOP_PUT, ACK_POS, ACK_NEG, CARGO
 *  seq: sequence number
 *  ack: sequence number of an acknowledged packet
 *  pktleft: how many cargo packets the operation should use
 *  size: length of data field
 *  data: payload
 *
 *  return: a packet with parameters
 *  error:
 */
struct pkt makepkt(int op, int seq, int ack, int pktleft, size_t size, void *data){
    struct pkt packet;

    packet.op = op;
    packet.seq = seq;
    packet.ack = ack;
    packet.pktleft = pktleft;
    packet.size = size;
    memcpy(&packet.data, data, size);

    return packet;
}

/*
 *  function: calculate_filelength
 *  ----------------------------
 *  Calculate the length of a specified file
 *
 *  pathname: file to inspect
 *
 *  return: length of the file
 *  error: -1
 */
int calculate_filelength(char *pathname){
    struct stat finfo;
    int filelength = -1; // TODO size_t/ssize_t

    if(stat(pathname, &finfo) == 0){
        filelength = finfo.st_size;
    }

    return filelength;
}

/*
 *  function: calculate_numpkts
 *  ----------------------------
 *  Calculate how many packets are needed for a file
 *
 *  pathname: file to inspect
 *
 *  return: quantity of the packets the file is made of
 *  error: -1
 */
int calculate_numpkts(char *pathname){
    int numpkts = -1;

    int filelength = calculate_filelength(pathname);
    if(filelength != -1){
        numpkts = filelength / (DATASIZE);
        if((filelength % (DATASIZE)) != 0 || numpkts == 0) ++numpkts;
    }

    return numpkts;
}

/*
 *  function: check
 *  ----------------------------
 *  Evaluate a function with integer return
 *
 *  exp: variable or function to evaluate
 *
 *  return: exp without touching it
 *  error: exit
 */
int check(int exp, const char *msg){
    if(exp < 0){
        perror(msg);
        fprintf(stderr, "Error code %d\n", errno);
        exit(EXIT_FAILURE);
    }
    return exp;
}

/*
 *  function: check_mem
 *  ----------------------------
 *  Evaluate a function with pointer return, same as check above
 *
 *  mem: memory area to
 *
 *  return: exp without touching it
 *  error: exit
 */
void *check_mem(void *mem, const char *msg){
    if(mem == NULL){
        perror(msg);
        fprintf(stderr, "Error code %d\n", errno);
        exit(EXIT_FAILURE);
    }
    return mem;
}

/* Read "n" bytes from a descriptor. */
ssize_t readn(int fd, void *vptr, size_t n) {
    size_t nleft;
    ssize_t nread;
    char *ptr;

    ptr = vptr;
    nleft = n;
    while (nleft > 0) {
        if ((nread = read(fd, ptr, nleft)) < 0){ // ERROR
            if (errno == EINTR)
                nread = 0;
            else
               return (-1);
        } else if(nread == 0) // EOF
            break;

        nleft -= nread;
        ptr += nread;
        //ALERT :if((int)nread<(int)n) break;
        /* Se leggi di meno non bloccare ed esci dal ciclo*/
    }
    return (n - nleft);
}

/* Write "n" bytes to a descriptor. */
 ssize_t writen(int fd, const void *vptr, size_t n){
     size_t nleft;
     ssize_t nwritten;
     const char *ptr;
     ptr = vptr;
     nleft = n;
     while (nleft > 0) {
         if ( (nwritten = write(fd, ptr, nleft)) <= 0) {
             if (nwritten < 0 && errno == EINTR)
                 nwritten = 0;   /* and call write() again */
             else
                 return (-1);    /* error */
          }
          nleft -= nwritten;
          ptr += nwritten;
     }
     return (n);    /* byte ancora da scrivere*/
 }

 /*
  *  function: setsock
  *  ----------------------------
  *  Create a socket with defined address (even for server purpose) and timeout
  *
  *  addr: pointer to address to fill
  *  address: address of the host to contact
  *  port: port of the host to contact
  *  seconds: time for timeout
  *  is_server: flag useful for server purpose (address = ANY and bind on a defined port)
  *
  *  return: descriptor of a new socket
  *  error: 0
  */
int setsock(struct sockaddr_in *addr, char *address, int port, int seconds, int is_server){
    int sockd;
    struct timeval tout;

    sockd = check(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), "setsock:socket");

    //check_mem(memset((void *)&addr, 0, sizeof(addr)), "setsock:memset");
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);

    if(is_server){
        addr->sin_addr.s_addr = htonl(INADDR_ANY);

        check(bind(sockd, (struct sockaddr *)addr, sizeof(struct sockaddr)), "setsock:bind");
printf("[Server] Ready to accept on port %d (sockd = %d)\n\n", port, sockd);
    }else{
        check(inet_pton(AF_INET, address, &addr->sin_addr), "setsock:inet_pton");
printf("[Client] Ready to contact %s at %d.\n", address, port);
    }

    tout.tv_sec = seconds;
    tout.tv_usec = 0;
    check(setsockopt(sockd, SOL_SOCKET, SO_RCVTIMEO, &tout, sizeof(tout)), "setsock:setsockopt");

    return sockd;
}

void fflush_stdin() {
	char c;
	while(( c=getchar()) != EOF && c != '\n');
}
