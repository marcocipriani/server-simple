#define MAXTRANSUNIT 1500
#define HEADERSIZE 5*sizeof(int)
#define DATASIZE MAXTRANSUNIT-HEADERSIZE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/dir.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

char rcvbuf[45000]; //buffer per la put
int npkt = 31;


int main(int argc, char const *argv[]) {
	off_t filelength;
	int filesize;
	int byteslastpkt;
	char *arg;
	size_t totpktsize;
	int res;
	struct stat filestat;
	
	arg = malloc(DATASIZE*sizeof(char));
	printf("Type filename to put and press ENTER: ");
    fscanf(stdin, "%s", arg);
	stat(arg, &filestat);
	filelength = filestat.st_size;
	
	totpktsize =(size_t) (npkt*sizeof(char))*(DATASIZE*sizeof(char));
	res = sizeof(rcvbuf)-totpktsize;
	if (res >= 0) {
		printf("ok ci entra grandezza: %d", res);
		return 1;
	} else return 0;
	
	
	
	if (filelength == 0) {
			printf("File not found, check for correct pathname \n");
	} else {
		printf("File selected is %s and its size is %d \n", arg, filelength);
		filesize =(int) (filelength/((off_t) DATASIZE)); 
		if (filesize <=0) {        
			filesize = 1;
		}
		else if((byteslastpkt = (int) (filelength%((off_t)DATASIZE))) != 0) { 
				filesize++;
				printf("Last packet size will be %d bytes \n",byteslastpkt);
			} else {
				printf("File size perfectly match with pkt size! \n");
			}
		printf("Number of packet that will be send for file %s is %d \n",arg, filesize); 
		} 
	
}
