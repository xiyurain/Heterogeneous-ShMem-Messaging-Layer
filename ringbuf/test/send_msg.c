#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/syscalls.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/jiffies.h>

#define IOCTL_MAGIC		('f')
#define IOCTL_RING		_IOW(IOCTL_MAGIC, 1, u32)
#define IOCTL_REQ		_IO(IOCTL_MAGIC, 2)
#define IOCTL_IVPOSITION	_IOR(IOCTL_MAGIC, 3, u32)

extern unsigned long volatile jiffies;
struct workqueue_struct *write_workqueue;
static void write_msg(struct work_struct *work);
DECLARE_WORK(write_work, write_msg);

static void write_msg(struct work_struct *work) 
{
        struct file *fp = NULL;
        int i, cyc = 50;
        long ivposition;
        long unsigned int value;

        fp = filp_open("/dev/ringbuf", O_RDWR, 0644);
        ivposition = fp->f_op->unlocked_ioctl(fp, IOCTL_IVPOSITION, 0);
        if(ivposition == 1) {
                value = 2 << 16;
        } else {
                value = 1 << 16;
        }
        msleep(10000);
        printk(KERN_INFO "send_message test case start.\n");
        for(i = 0; i < cyc; i++) {
                // printk(KERN_INFO "11111111\n");
                fp->f_op->unlocked_ioctl(fp, IOCTL_REQ, value);
                msleep(2000);
        }
        
        filp_close(fp, NULL);
        fp = NULL;
}
 
int __init sendmsg_init(void)
{
        write_workqueue = create_workqueue("write_workqueue");
        queue_work(write_workqueue, &write_work);
        return 0;
}

void __exit sendmsg_exit(void)
{
    printk("send_message test case exit/n");
}
 
module_init(sendmsg_init);
module_exit(sendmsg_exit);
 
MODULE_LICENSE("GPL");