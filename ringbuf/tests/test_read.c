#include<stdio.h>
#include<fcntl.h>
#include<stdlib.h>

void main(void)  
{  
    int fd;  
    int i;  
    char data[256];  
    int retval;  

    fd = open("/dev/ringbuf", O_RDONLY);  
    if(fd == -1) {  
        perror("error open\n");  
        exit(-1);  
    }  
    printf("TEST: open /dev/ringbuf successfully\n");  

    
    retval = read(fd,data,3);  
    if(retval == -1) {  
        perror("read error\n");  
        exit(-1);  
    }  
    data[retval] = 0;  
    printf("read successfully:%s\n", data);  


    close(fd);  
}