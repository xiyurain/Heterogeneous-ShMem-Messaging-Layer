#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
 
int __init rtest_init(void)
{
    struct file *fp;
    printk("hello enter/n");

    fp = filp_open("/dev/ringbuf", O_RDWR, 0644);
    if (IS_ERR(fp)){
        printk("create file error/n");
        return -1;
    }

    filp_close(fp,NULL);
    return 0;
}
void __exit rtest_exit(void)
{
    printk("rtest exit/n");
}
 
module_init(rtest_init);
module_exit(rtest_exit);
 
MODULE_LICENSE("GPL");