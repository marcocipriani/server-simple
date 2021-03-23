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

int check(int exp, const char *msg){
    if(exp < 0){
        perror(msg);
        fprintf(stderr, "Error code %d\n", errno);
        exit(EXIT_FAILURE);
    }
    return exp;
}
