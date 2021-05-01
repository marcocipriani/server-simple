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
#include <sys/ipc.h>
#include <sys/sem.h>
#include <signal.h>

#include "macro.h"

struct pkt{
    int op; // op codes in macro.h
    int seq;
    int ack;
    int pktleft;
        // client synop-list:notset synop-get:notset synop-put:totalpackets synack:ack.pktleft cargo:relativepktnumber ack-cargo:cargo.pktleft
        // server ack-list:totalpackets ack-get:totalpackets ack-put:synop.pktleft cargo:relativepktnumber ack-cargo:cargo.pktleft
    int size;
    char data[DATASIZE];
        // client synop-list:notset synop-get:pathname synop-put:pathname synack:pathname cargo:data ack-cargo:notset
        // server ack-list:operationstatus ack-get:operationstatus ack-put:operationstatus cargo:data ack-cargo:notset
};

// used by main thread to pass operation data to function thread
struct elab{
    struct sockaddr_in cliaddr;
    struct pkt clipacket;
};

// TODEL after thread_sendpkt changes to thread_info
struct elab2{
    int initialseq;   //numero sequenza iniziale per un dato file
    int *p;          //puntatore a array di contatori (ricezione ack)
    struct pkt thpkt;
};

// RTT
struct sample{
    struct timespec *start;
    int seq;
};

struct stack_elem{
    struct pkt packet;
    struct stack_elem *next;
};
typedef struct stack_elem *pktstack;

void push_pkt(pktstack *topPtr, struct pkt data){
    pktstack newPtr;     //puntatore al nuovo nodo

    newPtr = malloc(sizeof(struct stack_elem));
    if (newPtr != NULL){
        newPtr->packet = data;
        newPtr->next = *topPtr;
        *topPtr = newPtr;
    }else{// no space available
printf("pacchetto %d non inserito nella pila\n",data.seq);
    }
}

struct pkt pop_pkt(pktstack *topPtr){
    pktstack tempPtr; //puntatore temporaneo al nodo
    struct pkt popValue; //pacchetto rimosso

    tempPtr = *topPtr;
    popValue = (*topPtr)->packet;
    *topPtr = (*topPtr)->next;
    free(tempPtr);

    return popValue;
}

struct index{
    int value;
    struct index *next;
};
typedef struct index *index_stack;

// TODO push_index
// TODO pop_index

struct sender_info{
    pktstack stack; //puntatore a struct stack_elem
    int semLoc;              //descrittore semaforo local
    int semTimer;
    pthread_mutex_t mutex_stack;
    pthread_mutex_t mutex_ack_counter;
    //pthread_mutex_t mutex_rtt;
    int *ack_counters;      //contatore di ack
    int *base;               //numero sequenza in push_base
    int initialseq;         //numero di sequenza iniziale
    int numpkt;
    int sockid;             //socket a cui spedire pkt
    int *timer;
    double *estimatedRTT;
    struct sample *startRTT;
    //struct timespec *endRTT;
    double *timeout_Interval;
    pid_t father_pid;       //pid del padre
};

struct receiver_info{
    int numpkts; // total packets of the file to be received
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
 *  function: freespacebuf
 *  ----------------------------
 *  Check if there's enough space in the receiver buffer
 *
 *  return: quantity of rcvbuf cells are free
 *  error: -1
 */
 // TODO scan free indexes stack
 // -> int freespacebuf(struct  *free_indexes, int totpkt)
int freespacebuf(index_stack buf_index){
    size_t totpktsize;
    char rcvbuf[DATASIZE];
    int res;

    //totpktsize = (size_t) (totpkt*sizeof(char))*(DATASIZE*sizeof(char));
    //res = sizeof(rcvbuf)-totpktsize;
    if(res >=0){
        return 1;
    }else{
        return 0;
    }
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

/*
 *  function: readn
 *  ----------------------------
 *  Read n bytes from a file
 *
 *  fd: file descriptor to read from
 *  buf: buffer to save read bytes
 *  n: number of bytes to read
 *
 *  return: number of bytes actually read
 *  error: -1
 */
ssize_t readn(int fd, void *buf, size_t n) {
    size_t nleft;
    ssize_t nread;
    char *ptr;

    ptr = buf;
    nleft = n;
    while (nleft > 0) {
        if ((nread = read(fd, ptr, nleft)) < 0){ // error
            if (errno == EINTR) // read interrupted by a signal
                nread = 0;
            else
               return (-1);
        } else if(nread == 0) // EOF
            break;

        nleft -= nread;
        ptr += nread;
        //ALERT :if((int)nread<(int)n) break;
        // Se leggi di meno non bloccare ed esci dal ciclo
    }
    return (n - nleft);
}

/*
 *  function: writen
 *  ----------------------------
 *  Write n bytes to a file
 *
 *  fd: file descriptor to write onto (write destination)
 *  vptr: buffer with bytes to write (write source)
 *  n: number of bytes to write
 *
 *  return: number of bytes actually written TODO ?
 *  error: -1
 */
ssize_t writen(int fd, const void *vptr, size_t n){
    size_t nleft;
    ssize_t nwritten;
    const char *ptr;
    ptr = vptr;
    nleft = n;

    while (nleft > 0) {
        if((nwritten = write(fd, ptr, nleft)) <= 0){
            if (nwritten < 0 && errno == EINTR)
                nwritten = 0; // and call write() again
        else
            return (-1); // error
        }
        nleft -= nwritten;
        ptr += nwritten;
    }
    return n; // bytes still to read
}

/*
 *  function: setsock
 *  ----------------------------
 *  Create a socket with defined address (even for server purpose) and timeout
 *
 *  addr: address of contacting end point
 *  seconds: time for timeout
 *  is_server: flag for bind()
 *
 *  return: descriptor of a new socket
 *  error: -1
 */
int setsock(struct sockaddr_in addr, int seconds, int is_server){
    int sockd = -1;
    struct timeval tout;

    sockd = check(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), "setsock:socket");

    // check_mem(memset((void *)&addr, 0, sizeof(addr)), "setsock:memset");
    // addr->sin_family = AF_INET;
    // addr->sin_port = htons(port);
    // addr->sin_addr.s_addr = htonl(INADDR_ANY);
    // check(inet_pton(AF_INET, address, &addr->sin_addr), "setsock:inet_pton");
    if(!is_server){
        check(bind(sockd, (struct sockaddr *)&addr, sizeof(struct sockaddr)), "setsock:bind");
    }

    tout.tv_sec = seconds;
    tout.tv_usec = 0;
    check(setsockopt(sockd, SOL_SOCKET, SO_RCVTIMEO, &tout, sizeof(tout)), "setsock:setsockopt");

printf("Created new socket id:%d family:%d port:%d addr:%d\n", sockd, addr.sin_family, addr.sin_port, addr.sin_addr.s_addr);
    return sockd;
}

/*
 *  function: fflush_stdin
 *  ----------------------------
 *  Implementation safe for fflush
 *
 *  return: -
 *  error: -
 */
void fflush_stdin() {
	char c;
	while( (c=getchar())!=EOF && c!='\n');
}
