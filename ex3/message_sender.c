#include "message_slot.h"    

#include <fcntl.h>      /* open */ 
#include <unistd.h>     /* exit */
#include <sys/ioctl.h>  /* ioctl */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv)
{
    int file_desc;
    int ret_val;

    if (argc != 4){
        perror("ERROR: Wrong number of arguments");
        exit(1);
    }

    file_desc = open(argv[1], O_WRONLY );
    if( file_desc < 0 ) 
    {
        perror("ERROR: Can't open device file");
        exit(1);
    }

    ret_val = ioctl( file_desc, MSG_SLOT_CHANNEL, atoi(argv[2]));
    if (ret_val !=0){
        perror("ERROR: ioctl command failed");
        exit(1);
    }
    ret_val = write( file_desc, argv[3], strlen(argv[3]));

    if (ret_val != strlen(argv[3])){
        perror("ERROR: write command failed");
        exit(1);
    }    
    close(file_desc); 
    return 0;
    }
