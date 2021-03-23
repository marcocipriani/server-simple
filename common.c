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
