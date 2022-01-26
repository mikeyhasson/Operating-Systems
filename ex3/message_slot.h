#ifndef MSG_SLOT_H
#define MSG_SLOT_H

#include <linux/ioctl.h>

// The major device number.
// We don't rely on dynamic registration
// any more. We want ioctls to know this
// number at compile time.
#define MAJOR_NUM 240
#define NUMBER_OF_MSG_SLOTS 256

// Set the message of the device driver
#define MSG_SLOT_CHANNEL _IOW(MAJOR_NUM, 0, unsigned long)

#define BUF_LEN 128
#define DEVICE_RANGE_NAME "msg_slot"
#define SUCCESS 0

#endif
