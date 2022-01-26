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
    char buffer [BUF_LEN];

    if (argc != 3){
        perror("ERROR: Wrong number of arguments");
        exit(-1);
    }

    file_desc = open( argv[1], O_RDONLY );
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
    ret_val = read( file_desc, buffer,BUF_LEN);
    if (ret_val <= 0){
        perror("ERROR: read command failed");
        exit(1);
    }

    if (write(STDOUT_FILENO, buffer, ret_val) != ret_val) {
        perror("ERROR: printing message command failed");
		exit(1);
	}

    close(file_desc);
    return 0;
    }
