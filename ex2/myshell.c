#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#define _GNU_SOURCE
#define EXECUTE_IN_BACKGROUND '&'
#define REDIRECTION_SYMBOL '>'
#define PIPE_SYMBOL '|'

void handle_exception (char* str, int sh_flag){
    fprintf(stderr, str, strerror(errno));
    if (sh_flag){
        exit(1);
    }
}
void remove_zombies (int signum) {
    int i = errno;
    while (waitpid(-1, 0, WNOHANG) > 0) {};
    errno =i;
}

void sigchld_handler() {
    struct sigaction new_action;
    memset(&new_action, 0, sizeof(new_action));
    new_action.sa_flags = SA_NOCLDSTOP|SA_RESTART;// only get the SIGCHLD upon child death + disable EINTR;
    new_action.sa_handler = &remove_zombies;
    if(sigaction(SIGCHLD, &new_action, NULL) == -1){
        handle_exception("Exeption in sigaction",1);
    }   
}   
/*
* flag SA_RESTART prevents fail with the error EINTR.
* ignoring SIGINT.
*/
void sigint_handler() {
    struct sigaction new_action;
    memset(&new_action, 0, sizeof(new_action));
    new_action.sa_flags = SA_RESTART;  
    new_action.sa_handler=SIG_IGN;//ignore

    if(sigaction(SIGINT, &new_action, NULL) == -1){
        handle_exception("Exeption in sigaction",1);
    }   
}   
/*
change SIGINT signal handler to default so that foreground child processes (regular commands or parts of a pipe) would terminate upon SIGINT.
*/

void sigint_foreground_handler() {
    struct sigaction new_action;
    memset(&new_action, 0, sizeof(new_action));
    new_action.sa_handler =SIG_DFL;
    new_action.sa_flags = SA_RESTART; //as before, to prevent EINTR    
    if (sigaction(SIGINT, &new_action, NULL)==-1){
        handle_exception("Exeption in sigaction",1);
    }
}

// prepare and finalize calls for initialization and destruction of anything required
int prepare(void){
    sigchld_handler();
    sigint_handler();
    return 0;
}

int fork_and_check_pid (pid_t * pid_ptr) {
    pid_t pid = fork();

    if (pid < 0) {
        handle_exception("Error invoking fork",0);
        return 1;
    }
    *pid_ptr = pid;
    return 0;
}

int execute_command_in_background (int count,char** arglist){
    pid_t pid;

    arglist[count-1]=NULL;
    if (fork_and_check_pid(&pid)) {
        return 0;
    }
    
    if (!pid){
        //child, execute current command
        execvp(arglist[0],arglist);
        handle_exception("Error executing command",0);
        return 0;
    }
    return 1;
}

int execute_command (char** arglist){
    pid_t pid;

    if (fork_and_check_pid(&pid)) {
        return 0;
    }
    if(pid) {//parent, hold
        waitpid(pid, NULL, WUNTRACED);
    }
    else{ //child, execute current command
        sigint_foreground_handler();
        execvp(arglist[0],arglist);
        handle_exception("Error executing command",0);
        return 0;
        }
    return 1;
}

int output_redirecting (int count, char** arglist,char* output_filename){
    int   fd,pid;
    arglist[count-2] = NULL;

    if (fork_and_check_pid(&pid)) {
        return 0;
    }
    if (pid == 0) {
        sigint_foreground_handler();
        fd = open( output_filename, O_WRONLY | O_CREAT | O_TRUNC ,0640);
        if (fd == -1){
            handle_exception("Error creating output file",0);
            return 0;
        }
        if (dup2 (fd,1)== -1){//1=stdout goes to fd
            handle_exception("Error in func dup2",0);
            close(fd);
            return 0;
        } 
        close(fd);
        execvp(arglist[0],arglist);
        handle_exception("Error executing command",0);
        return 0;
    }   

    else {
        waitpid(pid, NULL, WUNTRACED);
    }
    return 1;
}


int single_piping(int count, char** arglist,int pipe_index){
    pid_t  pid_1,pid_2;
    int pipefd[2];
    char** arglist_1 = arglist;
    char** arglist_2 = arglist + pipe_index + 1;
    arglist[pipe_index] = NULL; 
    
    if( -1 == pipe( pipefd ) ){
        handle_exception("Exception invoking pipe",0);
        return 0;
    }

    if (fork_and_check_pid(&pid_1)) {
        return 0;
    }
    if( pid_1 == 0 ) {//Child_1
        sigint_foreground_handler();
        close( pipefd[0] ); //child is writing, close reading.
        if (dup2 (pipefd[1],1) == -1) {//  1=stdout refers to the same fd as pipefd[1] (the write end)
            handle_exception("Error executing command",0);
            return 0;
        }      
        close(pipefd[1]);
        execvp(arglist_1[0],arglist_1);
        handle_exception("Error executing command",0);
        return 0;
    }
    else {//parent
        close(pipefd[1]); // parent is reading, close writing.
        if (fork_and_check_pid(&pid_2)) {
            return 0;
        }
        if( pid_2 == 0 ) {//Child_2 
            sigint_foreground_handler();
            if (dup2 (pipefd[0],0) == -1) {//0=stdin mapped to the read end.
                handle_exception("Error executing command",0);
                return 0;
            }            
            close(pipefd[0]);
            execvp(arglist_2[0],arglist_2);
            handle_exception("Error executing command",0);
            return 0;
        }
        else {//parent
            waitpid(pid_1, NULL, WUNTRACED);
            waitpid(pid_2, NULL, WUNTRACED);
        }
    }
    return 1;
}

// arglist - a list of char* arguments (words) provided by the user
// it contains count+1 items, where the last item (arglist[count]) and *only* the last is NULL
// RETURNS - 1 if should continue, 0 otherwise
int process_arglist(int count, char** arglist) {
    if (count >= 1 && arglist[count-1][0] == EXECUTE_IN_BACKGROUND) {
        return execute_command_in_background (count,arglist);
    }
    if (count >= 2 && arglist[count-2][0] == REDIRECTION_SYMBOL) {
       return output_redirecting(count, arglist,arglist[count-1]);
    }
    for (int i=1;i<count-1;i++){
        if (arglist[i][0] == PIPE_SYMBOL){
            return single_piping(count, arglist,i);
        }
    }
    return execute_command (arglist);
}

int finalize(void){
    return 0;
}