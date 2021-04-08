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






void list(char** res, const char* path){
    /*char command[DATASIZE];
    FILE* file;

    sprintf(command, "ls %s | cat > list.txt", path);
    system(command);

    file = fopen("list.txt", "r");
    fread(*res, DATASIZE, 1, file);*/

      /* percorso della directory locale che contiene i files condivisi
      in questo caso uso la cartella del progetto*/
    	char *dirname = {"/home/fabio/Scrivania/progetto/server-simple"};
      char fl[20] = {"filelist.txt"};
      char buff[MAXLINE];
      char command[DATASIZE];
      FILE* file;
    	DIR * direc;
    	int fdir;
      int fdl;
      int n_entry,i;

    	struct dirent **filelist;
    	struct stat data;

    	errno=0;

      //stampo a schermo il contenuto del path
      sprintf(command, "ls %s | cat > list.txt", path);
      system(command);

      /* Alloca la memoria per memorizzare l'indirizzo del client*/
    	socklen_t len = (socklen_t) sizeof(struct sockaddr_in);
    	struct sockaddr_in *cliaddr = malloc(len);

    	if(cliaddr == NULL){
        exit_on_error("server: malloc cliaddr in list",errno);
      }

      /*Memorizza la struttura contenente l'indirizzo del client */
    	memcpy(cliaddr,arg,len);

    	/*Apri un directory stream*/

    	if ( (direc = opendir(dirname)) == NULL) {
    		perror("opendir");
    		exit(EXIT_FAILURE);
    	}
    	// basterebbe usare chdir(dirname path)
    	fdir = dirfd(direc);
    	fchdir(fdir);	 /*Per posizionarsi sulla directory interessata*/

     /*leggi il contenuto della directory,
     ordina in oridine alfabetico tutte le voci e salva l'indirizzo della lista ordinata in filelist*/

    	if((n_entry =scandir(dirname,&filelist,NULL,alphasort)) < 0)
    		exit_on_error("server:scandir",errno);

    /*Prendo il lock su mtx per per scrivere la filelist in modo atomico */

    	if(pthread_mutex_lock(&mtx) != 0)
    		exit_on_error("server:pthread_mutex_lock",errno);

     /*Crea un file che contiene la filelist*/

    	fdl = open("/home/fabio/Scrivania/progetto/server-simple/list.txt",O_CREAT | O_RDWR | O_TRUNC,0644);
    	if(fdl == -1)
    		exit_on_error("server:open shared_files.txt",errno);

    /*Scrivi la filelist*/

    	if (snprintf(buff,MAXLINE,"%80s\n\n", "FILELIST")<0)
    		exit_on_error("server:snprintf",errno);

    	if(writen(fdl,buff,strlen(f))<0)
    		exit_on_error("server:errore nella scrittura su file",errno);

    	if (snprintf(buff,MAXLINE,"\n%-60s%50s\n\n", "Name", "Dimension(byte)")<0)
    		exit_on_error("server:snprintf",errno);

    	if(writen(fdl,buff,strlen(f))<0)
    		exit_on_error("server:errore nella scrittura su file",errno);

    	for(i=0;i<n_entry;i++){
              /*azzera data  -> void *memset(void *s, int c, size_t n);*/
          		memset((void *)&data, 0, sizeof(data));

               /* stat Mi serve per ottenere le informazioni di ciascuna voce
               LA FUNZIONE -> int stat(const char *pathname, struct stat *statbuf); */
              if((stat(filelist[i]->d_name,&data)) ==-1)
          			exit_on_error("server:stat",errno);

              /*azzera il buffer*/
          		memset((void *)buff, 0, MAXLINE);

              /*Scrive la dimensione delle voci*/
          		if(snprintf(buff,MAXLINE,"\n%-60s%50lld\n\n", (filelist[i])->d_name,(long long)data.st_size)<0)
          			exit_on_error("server:snprintf",errno);

              /*Scrivi via via le voci su file*/
          		if(writen(fdl,buff,strlen(f))<0)
          			exit_on_error("server:errore nella scrittura su file",errno);
     }
     /*Dealloca il vettore di strutture dirent*/
    	while(--i > 0)
    		free(filelist[i]);
    	free(filelist);
      /* Chiudi il directory stream*/
    	if(closedir(direc) == -1)
    		exit_on_error("server:closedir",errno);

    /*Unlock del mutex*/

    	if(pthread_mutex_unlock(&mtx) != 0)
    		exit_on_error("server:pthread_mutex_unlock",errno);

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
                list(resptr, spath);
                sendack(sockd, cpacket->seq, /*listsize*/ 1, status);

printf("[Server] Sending list to client #%d...\n\n", cliaddr.sin_port);
                nextseqnum++;
                //pktlist = (struct pkt *)malloc(sizeof(struct pkt ));
                pktlist = makepkt(5,nextseqnum,1,1/*deve essere pacchetto residuo pktleft*/,res);
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
