#include "headers.h"
#include "config.h"

struct pkt{
    int op; // 0 cargo, 1 ack, 2 synop-list 3 synop-get 4 synop-put 5 synop-abort
    int seq;
    int ack;
    int pktleft; // previously status // synop-put:totalpackets cargo:transfernumber
    int datasize;
    char data[DATASIZE]; // synop: arg, ack:operationstatus (0 ok 1 denied 2 trylater) empty for ack
};

struct elab{
    struct sockaddr_in cliaddr;
    struct pkt clipacket;
};

struct pkt *makepkt(int seq, int ack, int flag, int op, void *data){
    struct pkt *packet;

    packet = (struct pkt *)malloc(sizeof(struct pkt));
    packet->op = op;
    packet->seq = seq;
    packet->ack = ack;
    packet->status = status;
    packet->datasize = strlen( (char *)data); // or sizeof?
    memcpy(packet->data, data, sizeof(data));

    return packet;
}

int check(int exp, const char *msg){
    if(exp < 0){
        perror(msg);
        fprintf(stderr, "Error code %d\n", errno);
        exit(EXIT_FAILURE);
    }
    return exp;
}
