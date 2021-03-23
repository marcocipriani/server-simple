#include "headers.h" // TODO everything needed?
#include "config.h"

struct pkt{
    int seq;
    int ack;
    int flag;
    int op;
    int length; // data lenght
    // TODO int winsize;
    char data[DATASIZE];
};

struct pkt *makepkt(int seq, int ack, int flag, int op, void *data){
    struct pkt *packet;

    packet = (struct pkt *)malloc(sizeof(struct pkt));
    packet->seq = seq;
    packet->ack = ack;
    packet->flag = flag;
    packet->op = op;
    packet->length = strlen( (char *)data);
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
