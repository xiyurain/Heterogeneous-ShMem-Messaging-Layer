#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
 
int __init wtest_init(void)
{
    // struct file *fp;
    printk("hello enter/n");

    // fp = filp_open("/dev/ringbuf", O_RDWR, 0644);
    // if (IS_ERR(fp)){
    //     printk("create file error/n");
    //     return -1;
    // }

    // filp_close(fp,NULL);

    // gic_raise_softirq(cpumask_of(0),24);
    return 0;
}
void __exit wtest_exit(void)
{
    printk("wtest exit/n");
}
 
module_init(wtest_init);
module_exit(wtest_exit);
 
MODULE_LICENSE("GPL");