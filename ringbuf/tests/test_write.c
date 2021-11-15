#include<stdio.h>
#include<fcntl.h>
#include<stdlib.h>

void main(void)  
{  
    int fd;  
    int i;  
    int retval;

    fd = open("/dev/ringbuf", O_WRONLY);  
    if(fd == -1) {  
        perror("error open\n");  
        exit(-1);  
    }  
    printf("TEST: open /dev/ringbuf successfully\n");  
    

    retval=write(fd, "Hi, this is a ringbuffer message", 33);  
    if(retval==-1) {  
        perror("write error\n");  
        exit(-1);  
    }  
    
    close(fd);  
}