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
#include <time.h>

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

// RTT
struct sample{
    struct timespec *start;
    int *seq;
};

struct stack_elem{
    struct pkt packet;
    struct stack_elem *next;
};
typedef struct stack_elem *pktstack;
int push_pkt(pktstack *s, struct pkt p){
    pktstack new = malloc(sizeof(struct stack_elem));
    if(new == NULL) return -1;

    new->packet = p;
    new->next = *s;
    *s = new;

    return 0;

}
int pop_pkt(pktstack *s, struct pkt *res){
    if(*s == NULL) return -1;

    *res = (*s)->packet;
    pktstack tmp = *s;
    *s = (*s)->next;

    free(tmp);
    return 0;
}

struct sender_info{
    pktstack *stack; //puntatore a struct stack_elem
    int sem_readypkts;              //descrittore semaforo local
    int semTimer;
    pthread_mutex_t mutex_time;
    pthread_mutex_t mutex_stack;
    pthread_mutex_t mutex_ack_counter;
    //pthread_mutex_t mutex_rtt;
    int *ack_counters;      //contatore di ack
    int *base;               //numero sequenza in push_base
    int initialseq;         //numero di sequenza iniziale
    int numpkts;
    int sockd;             //socket a cui spedire pkt
    int *timer;
    int *devRTT;
    int *estimatedRTT;
    struct sample startRTT;
    int *timeout_Interval;
    struct timespec *time_upload;
    pthread_t father_pid;       //pid del padre
    struct pkt *array;
    int *rwnd;
    int *nextseqnum;

};

struct index{
    int value;
    struct index *next;
};
typedef struct index *index_stack;
int push_index(index_stack *stack, int new_index){
    index_stack new = malloc(sizeof(struct index));
    if(new == NULL) return -1; // no fatal exit

    new->value = new_index;
    new->next = *stack;
    *stack = new;

    return 0;
}
int pop_index(index_stack *stack){
    if(*stack == NULL) return -1;

    int res = (*stack)->value;
    struct index *tmp = *stack;
    *stack = (*stack)->next;

    free(tmp);
    return res;
}
void init_index_stack(index_stack *stack, int n){
    for(int i=n-1;i>=0;i--){
        push_index(stack, i);
    }
};

struct queue_elem{
    struct pkt packet;
    struct queue_elem *next;
};
typedef struct{
    struct queue_elem *head;
    struct queue_elem *tail;
} pktqueue;
void init_queue(pktqueue *queue){
    queue->head = NULL;
    queue->tail = NULL;
}
int enqueue(pktqueue *queue, struct pkt packet){
    struct queue_elem *new_packet = malloc(sizeof(struct queue_elem));
    if(new_packet == NULL){ return -1; }

    new_packet->packet = packet;
    new_packet->next = NULL;

    if(queue->tail != NULL){
        queue->tail->next = new_packet;
    }
    queue->tail = new_packet;
    if(queue->head == NULL){
        queue->head = new_packet;
    }

    return 0;
}
int dequeue(pktqueue *queue, struct pkt *packet){
    if(queue->head == NULL){ return -1; }
    struct queue_elem *tmp = queue->head;

    *packet = tmp->packet;
    queue->head = queue->head->next;
    if(queue->head == NULL){
        queue->tail = NULL;
    }

    //free(tmp);
    return 0;
}

struct receiver_info{
    int sockd; // socket where to perform the operation
    int numpkts; // total packets of the file to be received
    int *nextseqnum; // sequence number to use for next send
    int sem_readypkts; // semaphore to see if there are some packets ready to be read
    int sem_writebase; // semaphore to write base packet (and next contingous processed packets) on rcvbuf
    pthread_mutex_t mutex_rcvqueue; // mutex for access to received packets queue
    pthread_mutex_t mutex_rcvbuf;
    pktqueue *received_pkts; // queue where are stored received packets
    int *file_cells; // list of cells from receive buffer where the file is stored in
    int init_transfer_seq; // sequence number of the first cargo packet
    int *rcvbase; // base number of the receive window (less recent packet to ack)
    int *last_packet_size; // size of last cargo in transfer, used for final write on file
    char *filename; // name of the file to receive
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
    memset(&packet.data, 0, DATASIZE);
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
ssize_t readn(int fd, void *buf, size_t n){
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
 *  return: number of bytes actually written
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
 *  Create a socket with defined address and timeout
 *
 *  addr: address of contacting end point
 *  seconds: time for timeout
 *
 *  return: descriptor of a new socket
 *  error: -1
 */
int setsock(struct sockaddr_in addr, int seconds){
    int sockd = -1;
    struct timeval tout;

    sockd = check(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), "setsock:socket");

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
void fflush_stdin(){
	char c;
	while( (c=getchar())!=EOF && c!='\n');
}

/*
 *  function: simulate_loss
 *  ----------------------------
 *  Roll a d100 to perform an operation, simulating a packet loss in sender
 *
 *  usage: if(simulate_loss()) send(...)
 *
 *  return: 1 (head) if operation will be performed, 0 (tail) if not
 *  error: -
 */
int h=1;
void seedpicker() {
    time_t seed;
    h+=1;
    seed = (time_t)h;
    srand(seed);
}
int simulateloss(int isClient){
    int i;
    if(isClient){
        seedpicker();
        i=((rand()%100)+1);
        printf("num.casuale: %d\n",i);
        if(i<=PACKET_LOSS_CLIENT){
            printf("\npacket lost by Client!\n");
            return 0;
        }else return 1;
    }else{
        h+=1;
        seedpicker();
        i=(((rand()+rand())%100)+1);
        if(i<=PACKET_LOSS_SERVER){
            printf("\npacket lost by Server!\n");
            return 0;
        }else return 1;
    }
}
