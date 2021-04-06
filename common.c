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

#include "config.h"

struct pkt{
    int op; // synop-abort:0 synop-list:1 synop-get:2 synop-put:3 ack:4 cargo:5
    int seq;
    int ack;
    int pktleft;
        // client synop-abort:notset synop-list:notset synop-get:notset synop-put:totalpackets cargo:relativepktnumber ack-list:notset ack-get:notset
        // server ack-list:totalpackets ack-get:totalpackets ack-put:notset cargo:relativepktnumber
    int size;
    char data[DATASIZE]; // synop: arg, ack:operationstatus (0 ok 1 denied 2 trylater) empty for ack
};

struct elab{
    struct sockaddr_in cliaddr;
    struct pkt clipacket;
};

struct pkt *makepkt(int op, int seq, int ack, int pktleft, void *data){
    struct pkt *packet;

    packet = (struct pkt *)malloc(sizeof(struct pkt));
    packet->op = op;
    packet->seq = seq;
    packet->ack = ack;
    packet->pktleft = pktleft;
    packet->size = strlen((char *)data);
    memcpy(packet->data, data, packet->size);

    return packet;
}

int calculate_numpkts(char *pathname){
    struct stat finfo;
    int numpkts = -1;

    if( stat(pathname, &finfo) == 0){
        numpkts = finfo.st_size / (DATASIZE);
        if((finfo.st_size % (DATASIZE)) != 0 || numpkts == 0) ++numpkts;
    }

    return numpkts;
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

int setsock(struct sockaddr_in *addr, char *address, int port, int seconds, int isServer){
    int sockd;
    struct timeval tout;

    sockd = check(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), "setsock:socket");

    //check_mem(memset((void *)&addr, 0, sizeof(addr)), "setsock:memset");
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);

    if(isServer){
        addr->sin_addr.s_addr = htonl(INADDR_ANY);

        check(bind(sockd, (struct sockaddr *)addr, sizeof(struct sockaddr)), "setsock:bind");
printf("[Server] Ready to accept on port %d (sockd = %d)\n\n", port, sockd);
    } else {
        check(inet_pton(AF_INET, address, &addr->sin_addr), "setsock:inet_pton");
printf("[Client] Ready to contact %s at %d.\n", address, port);
    }

    tout.tv_sec = seconds;
    tout.tv_usec = 0;
    check(setsockopt(sockd, SOL_SOCKET, SO_RCVTIMEO, &tout, sizeof(tout)), "setsock:setsockopt");

    return sockd;
}
