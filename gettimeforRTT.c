#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

typedef struct thread{
  int num_thread;
  struct timespec *start;
  struct timespec *end;
}thread_info;

void *calculate_RTT(void *arg){

  thread_info thread_i;
  thread_i = *((thread_info *)arg);

  if(thread_i.num_thread == 0){

      clock_gettime( CLOCK_REALTIME, thread_i.start );
      printf("thread_i %d \n",thread_i.num_thread);
      //printf("thread_i %d \n",&thread_i.start->tv_sec);
      printf("thread_i %ld \n",thread_i.start->tv_sec);
  }else{

      usleep(1999899);
      printf("thread_i %d \n",thread_i.num_thread);

      clock_gettime( CLOCK_REALTIME, thread_i.end );
      float RoundTripTime = (thread_i.end->tv_sec - thread_i.start->tv_sec) + (1e-9)*(thread_i.end->tv_nsec - thread_i.start->tv_nsec);
      printf("thread_i %ld \n",thread_i.end->tv_sec);
      //printf("thread_i %d \n",&thread_i.start->tv_sec);
      printf("tempo trascorso %lf \n",RoundTripTime);
  }
pthread_exit(NULL);
}


int main (void){

    struct timespec start;
    struct timespec end;
    pthread_t tid;
    int i;
    thread_info thread_i;

    for(i = 0; i<2 ;i++){
      thread_i.start = &start;
      thread_i.end = &end;
      thread_i.num_thread = i;
      if((pthread_create(&tid,NULL,calculate_RTT,(void *)&thread_i) ) != 0 ){
        printf("errore creazione thread \n");
      }
      sleep(1);

    }
    while(1){
      pause();
    }

}
