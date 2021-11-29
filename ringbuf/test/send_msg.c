#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/syscalls.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
 
int __init sendmsg_init(void)
{
        struct file *fp = NULL;
        loff_t pos = 0;
        printk("send_message test case start.\n");

        fp = filp_open("/dev/ringbuf", O_RDWR, 0644);
        if (IS_ERR(fp)) {
                printk("error occured while opening ring buffer, exiting...\n");
                return 0;
        }
        
        kernel_write(fp, "Xiangyu Ren - 180110718@stu.hit.edu.cn", 37, &pos);
        
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