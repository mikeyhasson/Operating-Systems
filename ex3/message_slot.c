#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE

#include <linux/kernel.h>   /* We're doing kernel work */
#include <linux/module.h>   /* Specifically, a module */
#include <linux/fs.h>       /* for register_chrdev */
#include <linux/uaccess.h>  /* for get_user and put_user */
#include <linux/string.h>   /* for memset. NOTE - not string.h!*/
#include <linux/radix-tree.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");

//Our custom definitions of IOCTL operations
#include "message_slot.h"

typedef struct minor_and_channel {
    unsigned int minor;
    unsigned int channel;
}  FD_CHANNEL;

typedef struct msg_and_length {
    char msg_buffer [BUF_LEN];
    int size;
} MSG;

static struct radix_tree_root* channels_by_minor [NUMBER_OF_MSG_SLOTS];

//================== DEVICE FUNCTIONS ===========================
static int device_open( struct inode* inode,
                        struct file*  file )
{
    FD_CHANNEL* fd_channel = kmalloc(sizeof(FD_CHANNEL),GFP_KERNEL);
    if (fd_channel == NULL){
        return 1;
    }
    fd_channel -> minor =iminor(inode);
    if (channels_by_minor[fd_channel -> minor] == NULL){
        channels_by_minor[fd_channel->minor] = kmalloc(sizeof(struct radix_tree_root), GFP_KERNEL);
        if (channels_by_minor[fd_channel->minor] == NULL){
            return 1;
        }
        INIT_RADIX_TREE(channels_by_minor[fd_channel -> minor],GFP_KERNEL);
    }
    fd_channel -> channel =0;
    file -> private_data = (void*)fd_channel;
    return SUCCESS;
}
//----------------------------------------------------------------
static long device_ioctl( struct   file* file,
                          unsigned int   ioctl_command_id,
                          unsigned long  channel_id )
{
    FD_CHANNEL* fd_channel = (FD_CHANNEL*) file -> private_data;

    if( (MSG_SLOT_CHANNEL != ioctl_command_id) || (channel_id == 0))
    {
        return -EINVAL;
    }
    fd_channel ->channel = (unsigned int)channel_id;
    return SUCCESS;
}

//---------------------------------------------------------------
static int device_release( struct inode* inode,
                           struct file*  file)
{
    kfree (file -> private_data);
    return SUCCESS;
}

//---------------------------------------------------------------
// a process which has already opened
// the device file attempts to read from it
static ssize_t device_read( struct file* file,
                            char __user* buffer,
                            size_t       length,
                            loff_t*      offset )
{
    MSG* msg;
    int err;
    FD_CHANNEL* fd_channel = (FD_CHANNEL*) file -> private_data;

    if (fd_channel ->channel == 0 || buffer == NULL){
        return -EINVAL;
    }

    msg = radix_tree_lookup(channels_by_minor[fd_channel -> minor],fd_channel->channel);

    if (msg == NULL){
        return -EWOULDBLOCK;
    }

    if (length < msg ->size){
        return -ENOSPC;
    }
    err = copy_to_user(buffer,msg -> msg_buffer,msg ->size);
    if (err == 0){
        return msg ->size;
    }
    return -EFAULT;
  
}

//---------------------------------------------------------------
// a processs which has already opened
// the device file attempts to write to it
static ssize_t device_write( struct file*       file,
                             const char __user* buffer,
                             size_t             length,
                             loff_t*            offset)
{
    MSG* msg;
    int i;
    FD_CHANNEL* fd_channel = (FD_CHANNEL*) file -> private_data;
    struct radix_tree_root* root =channels_by_minor[fd_channel -> minor];

    if (fd_channel ->channel==0 || buffer == NULL){
        return -EINVAL;
    }
    if (length == 0 || length >BUF_LEN){
        return -EMSGSIZE;
    }
    msg = radix_tree_lookup(root,fd_channel->channel);
    if (msg == NULL){
        msg = kmalloc (sizeof(MSG),GFP_KERNEL);
        msg -> size = 0;
        radix_tree_insert(root,fd_channel->channel,msg);
    }

    for( i = 0; i < length && i<BUF_LEN; ++i ){
        if(get_user(msg -> msg_buffer[i], &buffer[i]) != 0){
            msg->size=0;
            return -EFAULT;
        }
    }
    
    if (i != length){
        msg -> size = 0;
        return -EINVAL;
    }
    msg -> size=i;

    // return the number of input characters used
    return i;
}



//==================== DEVICE SETUP =============================

// This structure will hold the functions to be called
// when a process does something to the device we created
struct file_operations Fops =
{
  .owner	  = THIS_MODULE, 
  .read           = device_read,
  .write          = device_write,
  .open           = device_open,
  .unlocked_ioctl = device_ioctl,
  .release        = device_release,
};

//---------------------------------------------------------------
// Initialize the module - Register the character device
static int __init simple_init(void)
{
  int rc = -1;
  // Register driver capabilities. Obtain major num
  rc = register_chrdev( MAJOR_NUM, DEVICE_RANGE_NAME, &Fops );

  // Negative values signify an error
  if( rc < 0 )
  {
    printk( KERN_ERR "%s registraion failed for  %d\n",
                       DEVICE_RANGE_NAME, MAJOR_NUM );
    return rc;
  }
  return 0;
}

//---------------------------------------------------------------
//source:https://elixir.bootlin.com/linux/latest/source/mm/backing-dev.c#L701
static void __exit simple_cleanup(void)
{   
    int i;
    struct radix_tree_iter iter;
	void **slot;
    
    unregister_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME);
    for (i=0; i<NUMBER_OF_MSG_SLOTS;i++){
        if(channels_by_minor[i]!=NULL){
            radix_tree_for_each_slot(slot, channels_by_minor[i], &iter, 0) {
                kfree(*slot);
            }
        }
    }
}

//---------------------------------------------------------------
module_init(simple_init);
module_exit(simple_cleanup);

//========================= END OF FILE =========================
