#include "common.c"
#include "config.h"
#include <dirent.h>

/* variabili globali*/
pthread_mutex_t mtx;

int sockd;
int nextseqnum;
char *msg;
struct sockaddr_in cliaddr;
socklen_t len;

void setsock(){
    struct sockaddr_in servaddr;

    check(sockd = socket(AF_INET, SOCK_DGRAM, 0), "setsock:socket");

    check_mem(memset((void *)&servaddr, 0, sizeof(servaddr)), "setsock:memset");
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    check(bind(sockd, (struct sockaddr *)&servaddr, sizeof(servaddr)) , "setsock:bind");

fprintf(stdout, "[Server] Ready to accept on port %d\n\n", SERVER_PORT);
}


void sendack(int sockd, int cliseq, int pktleft, char *status){
    struct pkt *ack;

    nextseqnum++;
    ack = (struct pkt*) malloc(sizeof(struct pkt*));
    ack = (struct pkt *)check_mem(makepkt(4, nextseqnum, cliseq, pktleft, status), "sendack:makepkt");

    check(sendto(sockd, ack, HEADERSIZE+strlen(status), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr)) , "sendack:sendto");
printf("[Server] Sending ack [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", ack->op, ack->seq, ack->ack, ack->pktleft, ack->size, (char *)ack->data);
}

void get(struct pkt *reqdata){
    printf("I'm alive %d\n", getpid());
    printf("%s\n", reqdata->data);
}

void list(char** res, const char* path ,void * address){

    DIR *dp = NULL;
    struct dirent *dptr = NULL;
    FILE *file;
    int n_entry;
    char buff[128];
    char buffer[128];

    //PRENDO IL LOCK IL MUTEX
    if(pthread_mutex_lock(&mtx) != 0)
          exit_on_error("server:pthread_mutex_lock");

    // setto il buffer
    memset(buff,0,sizeof(buff));  //to 0

    //copio nel buffer il path dato in input ( nel caso del progetto viene da pacchetto)
    strcpy(buff,path);

    // Stream della directory SE C'È ERRORE -> NULL
    if((dp = opendir(path) == NULL ){
        printf("\n Errore nell'apertura della directory[%s]\n",path);
        exit(1);
    }
    else
    {
        printf("\n Il contenuto della directory [%s]\n",path);
        // Leggo il contenuto della directory
        while(NULL != (dptr = readdir(dp)) )
        { /*
          //ORDINAMENTO ALFABETICO DA SISTEMARE
          if((n_entry =scandir(path,&(dptr),NULL,alphasort)) < 0){
            printf("server-list:scandir\n");
            exit(-1);
          }*/

          sprintf(buffer, "ls %s | cat > list.txt", path);
          system(buffer);

          file = fopen("list.txt", "w");
          fwrite(dptr->d_name,DATASIZE,1,file);
        }

        // Chiudo lo stream-diretory
        if(closedir(dp) == -1)
          printf("server:closedir");
          exit(-1);
     }
    /*Dealloca il vettore di strutture dirent*/
    free(dptr);

    //RILASCIO IL MUTEX
    if(pthread_mutex_unlock(&mtx) != 0)
          exit_on_error("server:pthread_mutex_unlock");


    //ORA DEVO INVIARE RITORNARE IL "FILE" CONTENENTE
    //IL CONTENUTO DELLA DIRECTORY (PATH).
    //NEL MAIN L'INVIO DEL PACCHETTO È
    //SOLO DI char *res ( PACCHETTO SINGOLO)
    //deve inviare list.txt
    //...TO BE CONTINUED

}


int main(int argc, char const* argv[]) {
    char *spath = DEFAULT_PATH; // root folder for server
    struct pkt *cpacket;
    struct pkt *pktlist;
    struct pkt *ack;
    char * res;

    /* Usage */
    if(argc > 2){
        fprintf(stderr, "Path from argv[1] set, extra parameters are discarded. [Usage]: %s [<path>]\n", argv[0]);
    }

    /* Init */
    if(argc > 1) spath = (char *)argv[1];
printf("[Server] Root folder: %s\n", spath);
    nextseqnum = 0;
    setsock();
    cpacket = (struct pkt *)check_mem(malloc(sizeof(struct pkt)), "main:malloc:cpacket");
    len = sizeof(cliaddr);

    if (pthread_mutex_init(&mtx,NULL) != 0) {
      printf("Errore nella pthread_mutex_init \n");
      exit(-1);
    }

    // TMP for testing list - contenuto di list
    res = malloc((DATASIZE-1) * sizeof(char)); // client has to put \0 at the end
    char **resptr = &res;
    // TMP for testing ack status
    char *status = "ok";
    int filesize = 0;
    int listsize ;
    pthread_t tid;
    struct elab *reqdata;

    while(1){
        /* Infinite receiving  */

        check_mem(memset((void *)&cliaddr, 0, sizeof(cliaddr)), "main:memset");
        check(recvfrom(sockd, cpacket, HEADERSIZE+DATASIZE, 0, (struct sockaddr *)&cliaddr, &len), "main:rcvfrom");
printf("[Server] Received [op:%d][seq:%d][ack:%d][pktleft:%d][size:%d][data:%s]\n", cpacket->op, cpacket->seq, cpacket->ack, cpacket->pktleft, cpacket->size, cpacket->data);

        /* Operation selection */
        switch (cpacket->op) {
            case 1: // list

                // TMP for testing list
                list(resptr, spath, address);
                sendack(sockd, cpacket->seq,1, status);

printf("[Server] Sending list to client #%d...\n\n", cliaddr.sin_port);
                nextseqnum++;
                //pktlist = (struct pkt *)malloc(sizeof(struct pkt ));
                pktlist = makepkt(5,nextseqnum,1,1/*deve essere pacchetto residuo pktleft*/,res/*dovrebbe essere filelist.txt*/);
                printf("ECCO LA LIST:\n%s\n",pktlist->data);
                sendto(sockd, pktlist,DATASIZE, 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
//fine list

                break;
            case 2: // get
                reqdata->cliaddr = cliaddr;
                printf("reqdata->cliaddr %s\n", reqdata->cliaddr);
                reqdata->clipacket = *cpacket;
                printf("reqdata->cpacket %s\n", reqdata->clipacket.data);
                //pthread_create(&tid, NULL, get, reqdata);

                sendack(sockd, cpacket->seq, filesize, status);
printf("My job here is done\n\n");
                break;
            case 3: // put
                sendack(sockd,cpacket->seq, 0, status);
printf("My job here is done\n\n");
                break;
            default:
printf("[Server] Can't handle this packet\n\n");
                // SEND A NACK? to protect from wrong packets
                sendack(sockd,cpacket->seq, 0, "malformed packet");
                break;
        }
    }

    exit(EXIT_FAILURE);
}
