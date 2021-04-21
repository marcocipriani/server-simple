#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>

pthread_mutex_t mtx;


void exit_on_error(char *err){
  printf("Errore : %s \n",err);
  exit(-1);
}


int main(int argc, char* argv[] )
{
  if ( argc < 2){
    printf("Inserisci path directory \n");
    exit(-1);
  }

  char *path = argv[1];
  DIR *dirp = NULL;
  int fdl;
  int i;
  int n_entry;

  struct dirent **filename;

  //pthread Init
  if((pthread_mutex_init(&mtx,NULL)) != 0){
    exit_on_error("pthread_init");
  }


  //PRENDO IL LOCK IL MUTEX
  if(pthread_mutex_lock(&mtx) != 0)
        exit_on_error("server:pthread_mutex_lock");


  if ((dirp = opendir(path)) == NULL ){
      exit_on_error("nell'apertura della directory");

  }

  printf("CONTENUTO DELLA CARTELLA [%s] \n",path);
  /*Crea un file che contiene la filelist*/

  if ((fdl = open("list.txt",O_CREAT | O_RDWR | O_TRUNC,0644)) == -1){
      exit_on_error("server:open shared_files.txt");
  }

  if((n_entry =scandir(path,&filename,NULL,alphasort)) < 0){
      exit_on_error("server:scandir");
  }

  for(i = 0; i < n_entry; i++){
      printf("%s \n",filename[i]->d_name);
      dprintf(fdl,"%s\n",filename[i]->d_name);
  }

  closedir(dirp);
  dirp = NULL;



  //LASCIO IL MUTEX
  if(pthread_mutex_unlock(&mtx) != 0)
        exit_on_error("server:pthread_mutex_lock");


  return 0;
}
