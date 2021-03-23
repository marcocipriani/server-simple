#include "config.h"

struct pkt{
    int seq;
    int ack;
    int flag;
    int op;
    int length; // pkt lenght
    // TODO int winsize;
    char data[DATASIZE];
}
