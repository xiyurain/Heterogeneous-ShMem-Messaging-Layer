#include <linux/kfifo.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/interrupt.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pci_regs.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/spinlock_types.h>
#include <linux/spinlock.h>
#include <linux/genalloc.h>
#include <linux/string.h>                                       
#include <linux/slab.h>
#include "pcie.h"
#include "endpoint.h"
#include "sys.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Xiangyu Ren <180110718@mail.hit.edu.cn>");
MODULE_DESCRIPTION("ring buffer based on Inter-VM shared memory module");
MODULE_VERSION("1.0");

#define RINGBUF_SZ 512
#define MSG_SZ sizeof(rbmsg_hd)
#define BUF_INFO_SZ sizeof(ringbuf_info)
#define TRUE 1
#define FALSE 0
#define RINGBUF_DEVICE_MINOR_NR 0
#define MAX_MSG_TYPE 16
#define MIN_MSG_CTRLTYPE 8
#define MAX_SERVICE_NUM 8
#define MAX_SOCKET_NUM 64
#define QEMU_PROCESS_ID 1
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define SLEEP_PERIOD_MSEC 10
#define DEVNAME "ringbuf"

#define IOCTL_MAGIC		('f')
#define IOCTL_SOCKET		_IOW(IOCTL_MAGIC, 1, u32)
#define IOCTL_REQ		_IO(IOCTL_MAGIC, 2)
#define IOCTL_NODEID		_IOR(IOCTL_MAGIC, 3, u32)
#define IVPOSITION_REG_OFF	0x08
#define DOORBELL_REG_OFF	0x0c

static int NODEID = 0;
MODULE_PARM_DESC(NODEID, "Node ID of the shmem QEMU.");
module_param(NODEID, int, 0400);

/* Guest(read) or Host(write) role of ring buffer*/
enum {
	Guest	= 	0,
	Host	=	1,
};

/*API of the ringbuf device driver*/
static int __init ringbuf_init(void);
static void __exit ringbuf_cleanup(void);
static int ringbuf_open(struct inode *, struct file *);
static int ringbuf_release(struct inode *, struct file *);
static long ringbuf_ioctl(struct file *fp, unsigned int cmd, 
					long unsigned int value);
static int ringbuf_probe_device(
	struct pci_dev *pdev, const struct pci_device_id * ent);
static void ringbuf_remove_device(struct pci_dev* pdev);

/*other global variables*/
static int device_major_nr;
extern unsigned long volatile jiffies;

static const struct file_operations ringbuf_ops = {
	.owner		= 	THIS_MODULE,
	.open		= 	ringbuf_open,
	.release 	= 	ringbuf_release,
	.unlocked_ioctl   = 	ringbuf_ioctl,
};

static struct pci_device_id ringbuf_id_table[] = {
	{ 0x1af4, 0x1110, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0 },
};

static struct pci_driver ringbuf_pci_driver = {
	.name		= 	"RINGBUF",
	.id_table	= 	ringbuf_id_table,
	.probe	   	= 	ringbuf_probe_device,
	.remove	  	= 	ringbuf_remove_device,
};

MODULE_DEVICE_TABLE(pci, ringbuf_id_table);
module_init(ringbuf_init);
module_exit(ringbuf_cleanup);


static long ringbuf_ioctl(struct file *fp, unsigned int cmd,  long unsigned int value)
{
	int remote_id;
	ringbuf_endpoint *ep;

    	switch (cmd) {
    	case IOCTL_SOCKET://TODO
		
	case IOCTL_NODEID:
		printk(KERN_INFO "get NODEID: %d\n", NODEID);
		return (long)NODEID;

	default:
		printk(KERN_INFO "bad ioctl command: %d\n", cmd);
		return -1;
	}
	return 0;
}

static int ringbuf_open(struct inode * inode, struct file * filp)
{
	printk(KERN_INFO "Opening ringbuf device\n");

	if (MINOR(inode->i_rdev) != RINGBUF_DEVICE_MINOR_NR) {
		printk(KERN_INFO "minor number is %d\n", 
				RINGBUF_DEVICE_MINOR_NR);
		return -ENODEV;
	}
   	return 0;
}

static int ringbuf_release(struct inode * inode, struct file * filp)
{
	printk(KERN_INFO "release ringbuf_device\n");
   	return 0;
}

static int ringbuf_probe_device(struct pci_dev *pdev,
				const struct pci_device_id * ent) 
{
	int ret;
	ringbuf_endpoint *ep;
	
	printk(KERN_INFO "probing for device: %s@%d\n", (pci_name(pdev)), i);
	ret = pci_enable_device(pdev);
	if (ret < 0) {
		printk(KERN_INFO "unable to enable device: %d\n", ret);
		goto out;
	}
	ret = pci_request_regions(pdev, DEVNAME);
	if (ret < 0) {
		printk(KERN_INFO "unable to reserve resources: %d\n", ret);
		goto disable_device;
	}

	ep = endpoint_init(pdev);
	register_sys_handlers(ep);
	printk(KERN_INFO "device probed\n");
	return 0;

disable_device:
    	pci_disable_device(pdev);

out:
    	return ret;
}

static void ringbuf_remove_device(struct pci_dev* pdev)
{
	printk(KERN_INFO "removing ivshmem device\n");
	endpoint_destroy(pdev);

	pci_release_regions(pdev);
	pci_disable_device(pdev);	
}

static void __exit ringbuf_cleanup(void)
{
	pci_unregister_driver(&ringbuf_pci_driver);
	unregister_chrdev(device_major_nr, DEVNAME);
}

static int __init ringbuf_init(void)
{
    	int err = -ENOMEM;

	ringbuf_pci_driver.name = DEVNAME;
	err = register_chrdev(0, DEVNAME, &ringbuf_ops);
	if (err < 0) {
		printk(KERN_ERR "Unable to register ringbuf device\n");
		return err;
	}
	device_major_nr = err;
	printk("RINGBUF: Major device number is: %d\n", device_major_nr);

    	err = pci_register_driver(&ringbuf_pci_driver);
	if (err < 0) {
		goto error;
	}

	return 0;

error:
	unregister_chrdev(device_major_nr, DEVNAME);
	return err;
}
