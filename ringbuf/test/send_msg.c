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
 
int __init sendmsg_init(void)
{
        struct file *fp = NULL;
        loff_t pos = 0;
        int i, cyc = 20;
        char msg[256];

        fp = filp_open("/dev/ringbuf", O_RDWR, 0644);

        printk("send_message test case start.\n");
        for(i = 0; i < cyc; i++) {
                sprintf(msg, "Message Payload #%d", i);
                fp->f_op->write(fp, msg, strlen(msg) + 1, &pos);
                printk(KERN_INFO "message delivered: %s", msg);
                msleep(1000);
        }
        
        filp_close(fp, NULL);  
        fp = NULL;
        return 0;
}

void __exit sendmsg_exit(void)
{
    printk("send_message test case exit/n");
}
 
module_init(sendmsg_init);
module_exit(sendmsg_exit);
 
MODULE_LICENSE("GPL");