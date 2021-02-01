#include "headers.h" // TODO everything needed?

int check(int exp, const char *msg){
    if(exp < 0){
        perror(msg);
        exit(EXIT_FAILURE);
    }
    return 0;
}
