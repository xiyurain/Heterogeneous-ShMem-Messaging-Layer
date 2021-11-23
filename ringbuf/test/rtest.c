#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
 
int __init rtest_init(void)
{
	struct file *fp;
	mm_segment_t old_fs;
	char msg_buffer[64];

    	printk("hello enter/n");

    	fp = filp_open("/dev/ringbuf", O_RDWR, 0644);
    	old_fs = get_fs();
	set_fs(KERNEL_DS);

	vfs_read(fp, msg_buffer, 63, 0);

	set_fs(old_fs); 
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