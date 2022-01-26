#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <limits.h> 
#include <pthread.h>
#include <linux/limits.h>
#include <dirent.h>
#include <stdlib.h>
#include <errno.h>


#define NUMBER_OF_ARGS 3
#define SUCCESS 0
#define TYPE_QUEUE_THREADS 0
#define TYPE_QUEUE_DIRS 1
#define FOUND 1

typedef struct node_dir {
    struct node_dir* next;
    char* dir;
} NODE_DIR;

typedef struct node_thread {
    pthread_t thread_num;
    char* dir;
    int flag_enqueued;
    struct node_thread* next;
    pthread_cond_t* cond;
} NODE_THREAD;

typedef struct queue{
    void* head;
    void* tail;
    int type,size;
} QUEUE;

typedef struct queue_and_threads {
    int n_threads,err_count,matches,flag_terminate;   
    char* term;
    QUEUE* fifo;
    QUEUE* fifo_threads;
} THREADS;

//globals
pthread_mutex_t qlock;
pthread_mutex_t qlock_threads;
pthread_mutex_t thread_struct_lock;
pthread_cond_t all_init;


int enqueue_dir(QUEUE * fifo,NODE_DIR* cur_node, char* val) {
    if (cur_node == NULL){
        fprintf(stderr, "Error using malloc: %s",strerror(errno));
        return -1;
    }

    char* dir_name = (char*)malloc(PATH_MAX*sizeof(char));
    if (dir_name == NULL){
        fprintf(stderr, "Error using malloc: %s",strerror(errno));
        return -1;
    }
    strcpy(dir_name ,val);
    cur_node ->dir=dir_name;
    return SUCCESS;
}

void enqueue_thread(QUEUE * fifo,NODE_THREAD* cur_node) {
        cur_node ->dir =NULL;
        cur_node ->next =NULL;
        cur_node ->flag_enqueued=1;
}

int enqueue(QUEUE * fifo,void* val) {
    void* cur_node;
    if (fifo -> type == TYPE_QUEUE_DIRS){
        cur_node = malloc (sizeof(NODE_DIR));
        if (enqueue_dir(fifo,(NODE_DIR*) cur_node, (char*) val)==-1)
            return -1;    

        if (fifo->size !=0)
            ((NODE_DIR*)(fifo->tail)) ->next = cur_node;
    }
    else {
        cur_node = val;
        enqueue_thread(fifo,(NODE_THREAD*)cur_node);
        if (fifo->size !=0)
            ((NODE_THREAD*)(fifo->tail)) ->next = cur_node;
    }
        
    (fifo->size)++;


    if (fifo->size == 1){//queue is empty
        fifo -> head =cur_node;
        fifo ->tail= cur_node;
    }
    else {
        fifo ->tail = cur_node;
    }
    return SUCCESS;
}

void* dequeue(QUEUE * fifo) {
    char* dir_name;
    void *ret_val; 

    if (fifo -> type == TYPE_QUEUE_DIRS){
        NODE_DIR *head= fifo->head;
        fifo -> head = head ->next;

        dir_name = (char*) malloc (PATH_MAX *sizeof(char));    
        if (dir_name == NULL){
            fprintf(stderr, "Error using malloc: %s",strerror(errno));
            return NULL;
        }
        ret_val = (void*)(head->dir);
        free(head);
    }
    else {
        NODE_THREAD *head= fifo->head;
        fifo -> head = head ->next;
        head -> next =NULL;
        head ->dir =NULL;
        head -> flag_enqueued = 0;
        ret_val= (void*)head;
    }
    (fifo->size)--;
    if (fifo ->size == 0){
        fifo->head=NULL;
        fifo->tail=NULL;
    }
    return ret_val;
}


int enqueue_dir_check_permission (QUEUE* fifo, char* new_path){
   DIR *temp =opendir(new_path);

    if (temp == NULL){//diretory can't be serached
        printf("Directory %s: Permission denied.\n",new_path);
        return SUCCESS;
    }

    if (closedir(temp) == -1){
        fprintf(stderr, "Error closing directory: %s",strerror(errno));
        return -1;
    }   

    pthread_mutex_lock(&qlock);
    if (enqueue(fifo,new_path) == -1){
        pthread_mutex_unlock(&qlock);
        return -1;
    }

    pthread_mutex_unlock(&qlock);
    return SUCCESS;
}
int search_dir(THREADS* threads_struct, char* dir_name){
    char new_path[PATH_MAX];
    DIR *dir;
    struct dirent *cur_dir;
    struct stat statbuf;
    int matches=0;

    dir = opendir(dir_name);//should work due to "enqueue_dir_check_permission"

    while ((cur_dir = readdir(dir)) !=NULL){
        //if name is "." or "..", ignore it.
        if (strcmp(cur_dir->d_name,".") == 0 ||strcmp(cur_dir->d_name,"..") == 0)
            continue;
        
        sprintf(new_path, "%s/%s", dir_name, cur_dir->d_name);
        if (stat(new_path,&statbuf) !=SUCCESS) {
            fprintf(stderr, "Error using stat: %s",strerror(errno));
            return -1;
        }
        
        if(S_ISDIR(statbuf.st_mode)){//If the dirent is for a directory
            if (enqueue_dir_check_permission(threads_struct -> fifo,new_path) == -1) return -1;
        }
         // dirent is not for a directory - check for matches with term.
        else if (strstr(cur_dir->d_name, threads_struct ->term)){
            printf("%s\n",new_path);
            matches++;
            }
    }

    closedir(dir); 

    if (matches != 0){
        pthread_mutex_lock(&thread_struct_lock);
        threads_struct -> matches+=matches;
        pthread_mutex_unlock(&thread_struct_lock);
    }
    return SUCCESS;  
}
void exit_thread_err(THREADS* threads_struct,NODE_THREAD* node){
    pthread_cond_destroy(node ->cond);
    pthread_mutex_lock(&thread_struct_lock);
    threads_struct ->err_count++;
    pthread_mutex_unlock(&thread_struct_lock);
    pthread_exit(NULL);
}

int init_threads(int *lst_index_ptr,THREADS* threads_struct,NODE_THREAD* thread_node) {
    int lst_index;
    pthread_cond_t* cond_ptr = malloc(sizeof(pthread_cond_t));

    if (cond_ptr ==NULL){
        fprintf(stderr, "Error using malloc: %s",strerror(errno));
        pthread_mutex_unlock(&qlock);
        return -1;
    }

    thread_node -> cond = cond_ptr;

    if (thread_node ==NULL){
        fprintf(stderr, "Error using malloc: %s",strerror(errno));
        return -1;
    }

    thread_node ->thread_num = pthread_self();

    pthread_mutex_lock(&qlock);
    
    if (enqueue(threads_struct ->fifo_threads,thread_node) == -1) {
        pthread_mutex_unlock(&qlock);
        return -1;
    }
    pthread_cond_init(cond_ptr,NULL);
    lst_index=threads_struct -> fifo_threads ->size-1;
    *lst_index_ptr=lst_index;
    
    if(lst_index+1 == threads_struct ->n_threads)//marking all initialized
        pthread_cond_broadcast(&all_init);
    else //waiting until all threads are initialized.
        pthread_cond_wait(&all_init,&qlock);
    pthread_mutex_unlock(&qlock);

    return SUCCESS;
}

void exit_if_done(THREADS* threads_struct,NODE_THREAD* node){
    if (threads_struct ->fifo -> size ==0 && //no directory names in fifo
        threads_struct -> fifo_threads ->size  + threads_struct -> err_count //all threads are sleeping or exited do to error.
            == threads_struct -> n_threads) {
                if (!threads_struct -> flag_terminate){
                    NODE_THREAD* cur_node = threads_struct -> fifo_threads ->head;
                    while (cur_node!=NULL){
                        pthread_cond_signal(cur_node ->cond);//signal to wake all.
                        cur_node = (NODE_THREAD*)cur_node->next;
                    }
                    threads_struct -> flag_terminate=1;
                }
                pthread_cond_destroy(node ->cond);
                free(node);
                pthread_mutex_unlock(&qlock);
                pthread_exit(NULL);
            }
}

void* search_in_fifo(void*param){
    NODE_THREAD*cur_node,*thread_node;
    void* cur_dir;
    int lst_index;
    THREADS* threads_struct = (THREADS*)param;
    //inserting threads to list in the order they were initialized.
    //fifo of sleeping threads.

    thread_node = (NODE_THREAD*)malloc(sizeof(NODE_THREAD));
    if (init_threads(&lst_index,threads_struct,thread_node) == -1)
        exit_thread_err(threads_struct,thread_node);
    
    while(1){
        pthread_mutex_lock(&qlock);

        while (threads_struct ->fifo -> size !=0 && threads_struct ->fifo_threads -> size !=0){
            cur_dir = dequeue(threads_struct ->fifo);
            cur_node = (NODE_THREAD*)dequeue(threads_struct ->fifo_threads);

            if (cur_dir == NULL || cur_node == NULL){
                pthread_mutex_unlock(&qlock);
                exit_thread_err(threads_struct,thread_node);
            }
            cur_node -> dir = cur_dir;
            pthread_cond_signal(cur_node->cond);
        }        

        if (thread_node ->dir == NULL) {//if no directory was assigned
            if (threads_struct ->fifo -> size ==0){//no directotries to assign        
                if (!(thread_node -> flag_enqueued)) //if this thread was not sleeping make it sleep.
                    enqueue(threads_struct ->fifo_threads,(void*)thread_node);

                exit_if_done(threads_struct,thread_node); //check if current process is the last one to sleep and exit if so.
                pthread_cond_wait(thread_node -> cond,&qlock);//make it sleep untill new directories register.
                exit_if_done(threads_struct,thread_node); //thread_node -> cond is signaled also when we should terminate.
            }
            else{
                //There are direcotries to assign
                thread_node -> dir = (char*)dequeue(threads_struct ->fifo);
                if (thread_node -> dir == NULL){//errors check.
                    pthread_mutex_unlock(&qlock);
                    exit_thread_err(threads_struct,thread_node);
                    } 
                }
        }

        pthread_mutex_unlock(&qlock);
        //Flow of a searching thread
        if (search_dir(threads_struct,thread_node ->dir) == -1)
            exit_thread_err(threads_struct,thread_node);
        //set assigned directory to NULL
        thread_node ->dir=NULL;

    }

}

THREADS* init_threads_struct(char *argv[]){
    char * root_dir;
    DIR *dir;
    THREADS* threads_struct =(THREADS*)malloc(sizeof(THREADS));

    if (threads_struct == NULL){
        fprintf(stderr, "Error using malloc: %s",strerror(errno));
        exit(1);
    }

    threads_struct ->fifo_threads = (QUEUE*) malloc(sizeof(QUEUE));
    if (threads_struct->fifo_threads == NULL){
        fprintf(stderr, "Error using malloc: %s",strerror(errno));
        exit(1);
    }
    threads_struct ->fifo_threads ->type = TYPE_QUEUE_THREADS;

    threads_struct ->fifo = (QUEUE*) malloc(sizeof(QUEUE));
    if (threads_struct->fifo == NULL){
        fprintf(stderr, "Error using malloc: %s",strerror(errno));
        exit(1);
    }
    threads_struct ->fifo ->type = TYPE_QUEUE_DIRS;
    root_dir = argv[1];
    threads_struct ->term = argv[2];
    threads_struct ->n_threads = atoi(argv[3]);

    //check directory is readable.
    dir =opendir(root_dir);
    if (dir == NULL){
        fprintf(stderr, "Error opening directory: %s",strerror(errno));
        exit(1);
    }

    if (closedir(dir) == -1){
        fprintf(stderr, "Error closing directory: %s",strerror(errno));
        exit(1);
    }

    //enqueue root directory name.
    if (enqueue(threads_struct ->fifo,root_dir) == -1) exit(1);
    return threads_struct;
}

int exit_main(THREADS* threads_struct,pthread_t* thread_ids) {
    for(int i=0;i<threads_struct ->n_threads;i++){
        pthread_join(thread_ids[i], NULL);
    }

    printf("Done searching, found %d files\n",threads_struct ->matches);
    pthread_mutex_destroy(&thread_struct_lock);
    pthread_mutex_destroy(&qlock);
    pthread_mutex_destroy(&qlock_threads);
    pthread_cond_destroy(&all_init);
    if (threads_struct ->err_count !=0)
        exit(1);
    exit(0);
}

int main(int argc, char *argv[]) {
    
    pthread_t* thread_ids;
    THREADS* threads_struct;

    if (argc != (NUMBER_OF_ARGS+1)){
        fprintf(stderr, "Error, Wrong number of arguments: %s",strerror(EINVAL));
        exit(1);
    }
    threads_struct = init_threads_struct(argv);

    thread_ids = (pthread_t*)malloc((threads_struct ->n_threads) *sizeof(pthread_t));
    if (thread_ids == NULL){
        fprintf(stderr, "Error using malloc: %s",strerror(errno));
        exit(1);
    }

    pthread_mutex_init(&thread_struct_lock,NULL);
    pthread_mutex_init(&qlock,NULL);
    pthread_mutex_init(&qlock_threads,NULL);
    pthread_cond_init(&all_init,NULL);
    
    for(int i=0;i<threads_struct ->n_threads;i++){
        int rc = pthread_create(&thread_ids[i],NULL,search_in_fifo,(void*)threads_struct);
        if (rc !=SUCCESS){
            fprintf(stderr, "Error using pthread_create: %s",strerror(errno));
            exit(1);
        }
    }
    return exit_main(threads_struct,thread_ids);

}
