/*
 * drivers/char/ringbuf_ivshmem.c - driver of ring buffer based on Inter-VM shared memory PCI device
 *
 * Xiangyu Ren <180110718@mail.hit.edu.cn>
 *
 * Based on kvm_ivshmem.c:
 *         Copyright 2009 Cam Macdonell
 */

#include <linux/kfifo.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
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
#define QEMU_PROCESS_ID 1
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define SLEEP_PERIOD_MSEC 10

#define IOCTL_MAGIC		('f')
#define IOCTL_RING		_IOW(IOCTL_MAGIC, 1, u32)
#define IOCTL_REQ		_IO(IOCTL_MAGIC, 2)
#define IOCTL_IVPOSITION	_IOR(IOCTL_MAGIC, 3, u32)
#define IVPOSITION_REG_OFF	0x08
#define DOORBELL_REG_OFF	0x0c

static int ROLE = 1;
MODULE_PARM_DESC(ROLE, "Role of this ringbuf device.");
module_param(ROLE, int, 0400);

static int PCIID = 0;
MODULE_PARM_DESC(PCIID, "ID of this pci_dev in pci_name.");
module_param(PCIID, int ,0400);

static char *DEVNAME = "ringbuf";
MODULE_PARM_DESC(PCIID, "Name of this ringbuf device.");
module_param(DEVNAME, charp ,0400);

static int NODEID = 3;
MODULE_PARM_DESC(NODEID, "Node ID of the shmem QEMU.");
module_param(NODEID, int, 0400);

/* Guest(read) or Host(write) role of ring buffer*/
enum {
	Guest	= 	0,
	Host	=	1,
};

enum {
	msg_type_req 	=	1,
	msg_type_add	=	2,
	msg_type_free	=	3,
};

/*
 * message sent via ring buffer, as header of the payloads
*/
typedef struct ringbuf_msg_hd {

	unsigned int src_qid;
	unsigned int msg_type;

	unsigned int payload_off;
	ssize_t payload_len;

} rbmsg_hd;


typedef STRUCT_KFIFO(char, RINGBUF_SZ) fifo;

/*
 * @base_addr: mapped start address of IVshmem space
 * @regaddr: physical address of shmem PCIe dev regs
 * @ioaddr: physical address of IVshmem IO space
 * @fifo_host_addr: address of the Kfifo struct
 * @payload_area: start address of the payloads area
*/

typedef struct ringbuf_device {

	struct pci_dev		*dev;
	int			minor;

	u8 			revision;
	unsigned int 		ivposition;

	void __iomem 		*regs_addr;
	void __iomem 		*base_addr;

	unsigned int 		bar0_addr;
	unsigned int 		bar0_size;
	unsigned int 		bar1_addr;
	unsigned int 		bar1_size;

	char            	(*msix_names)[256];
	int             	nvectors;

	unsigned int 		bar2_addr;
	unsigned int 		bar2_size;

	fifo*			fifo_host_addr;
	fifo*			fifo_guest_addr;

	unsigned int 		*notify_guest_addr;
	unsigned int 		*notify_host_addr;
	unsigned int 		notify_guest_history;
	unsigned int 		notify_host_history;
	
	unsigned int 		role;
	void __iomem		*payload_area;
	struct gen_pool		*payload_pool;

} ringbuf_device;


static int __init ringbuf_init(void);
static void __exit ringbuf_cleanup(void);

static int ringbuf_open(struct inode *, struct file *);
static int ringbuf_release(struct inode *, struct file *);
static void ringbuf_remove_device(struct pci_dev* pdev);
static int ringbuf_probe_device(struct pci_dev *pdev,
				const struct pci_device_id * ent);

static long ringbuf_ioctl(struct file *fp, unsigned int cmd, 
					long unsigned int value);
static void ringbuf_poll(struct work_struct *work);
static void ringbuf_notify(void *addr);
static irqreturn_t ringbuf_interrupt(int irq);

static void recv_msg(struct tasklet_struct* data);
static unsigned int send_msg(fifo *fifo_addr, rbmsg_hd* hd, void *notify_addr);
static unsigned long add_payload(size_t len);
static void free_payload(rbmsg_hd *hd);

struct workqueue_struct *poll_workqueue;
DECLARE_WORK(poll_work, ringbuf_poll);
DECLARE_TASKLET(recv_msg_tasklet, recv_msg);

typedef int (*msg_handler)(rbmsg_hd *);
static msg_handler msg_handlers[16] = { NULL };
static void register_msg_handler(int msg_type, msg_handler handler);
static void unregister_msg_handler(int msg_type);

static int handle_msg_type_req(rbmsg_hd *hd);
static int handle_msg_type_add(rbmsg_hd *hd);
static int handle_msg_type_free(rbmsg_hd *hd);

static ringbuf_device ringbuf_dev;
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
MODULE_DEVICE_TABLE(pci, ringbuf_id_table);

static struct pci_driver ringbuf_pci_driver = {
	.name		= 	"RINGBUF",
	.id_table	= 	ringbuf_id_table,
	.probe	   	= 	ringbuf_probe_device,
	.remove	  	= 	ringbuf_remove_device,
};


static long ringbuf_ioctl(struct file *fp, unsigned int cmd,  long unsigned int value)
{
	ringbuf_device *dev = &ringbuf_dev;
	unsigned int req_id;
	unsigned int req_address;
	rbmsg_hd hd;

    	BUG_ON(dev->base_addr == NULL);

    	switch (cmd) {
    	case IOCTL_RING:
        	break;

	case IOCTL_REQ:
		if(dev->role == Host) {
			printk(KERN_INFO "Host CAN'T REQ!!");
			return -1;
		}
		req_id = (value >> 32) & 0xFFFFFFFF;
		req_address = value & 0xFFFFFFFF;

		hd.msg_type = msg_type_req;
		hd.src_qid = QEMU_PROCESS_ID;
		hd.payload_off = req_address;
		hd.payload_len = 0;

		send_msg(dev->fifo_guest_addr, &hd, dev->notify_host_addr);
		break;

	case IOCTL_IVPOSITION:
		printk(KERN_INFO "get ivposition: %d\n", NODEID);
		return (long)NODEID;

	default:
		printk(KERN_INFO "bad ioctl command: %d\n", cmd);
		return -1;
	}

	return 0;
}

static void ringbuf_poll(struct work_struct *work) {
	ringbuf_device *dev = &ringbuf_dev;

	switch (dev->role) {
	case Guest:
		while(TRUE) {
			if(*(dev->notify_host_addr) > dev->notify_host_history) {
				ringbuf_interrupt(25);
				dev->notify_host_history++;
			}
			msleep(SLEEP_PERIOD_MSEC);
		}
		break;
	case Host:
		while(TRUE) {
			if(*(dev->notify_guest_addr) > dev->notify_guest_history) {
				ringbuf_interrupt(25);
				dev->notify_guest_history++;
			}
			msleep(SLEEP_PERIOD_MSEC);
		}
		break;
	default: break;
	}
}

static inline void ringbuf_notify(void *addr) {
	(*(unsigned int*)addr)++;
}

static irqreturn_t ringbuf_interrupt(int irq)
{
	// printk(KERN_INFO "RINGBUF: interrupt: %d\n", irq);
	switch (irq)
	{
	case 25:
		tasklet_schedule(&recv_msg_tasklet);
		break;
	default:
		break;
	}
	
	return IRQ_HANDLED;
}

static int handle_msg_type_req(rbmsg_hd *hd) 
{
	fifo* fifo_addr = ringbuf_dev.fifo_host_addr;
	rbmsg_hd new_hd;
	char buffer[64];
	size_t len;

	sprintf(buffer, "msg #%2d - @peer%2d - (jiffies: %lu)",
			hd->payload_off, ringbuf_dev.ivposition, jiffies);
	len = strlen(buffer) + 1;

	new_hd.src_qid = QEMU_PROCESS_ID;
	new_hd.msg_type = msg_type_add;
	new_hd.payload_off = add_payload(len);
	new_hd.payload_len = len;

	memcpy(ringbuf_dev.payload_area + hd->payload_off, buffer, len);
	rmb();
	send_msg(fifo_addr, &new_hd, ringbuf_dev.notify_guest_addr);

	return 0;
}

static int handle_msg_type_add(rbmsg_hd *hd)
{
	fifo* fifo_addr = ringbuf_dev.fifo_guest_addr;
	char buffer[64];

	memcpy(buffer, ringbuf_dev.payload_area + hd->payload_off, 
						hd->payload_len);
	
	printk(KERN_INFO "RECEIVED     <<<= %s\n", buffer);

	hd->msg_type = msg_type_free;
	send_msg(fifo_addr, hd, ringbuf_dev.notify_host_addr);
	return 0;
}

static int handle_msg_type_free(rbmsg_hd *hd)
{
	free_payload(hd);
	return 0;
}

static void register_msg_handler(int msg_type, msg_handler handler)
{	
	if(handler == NULL || msg_type > 3 || msg_type < 1) {
		return;
	}
	msg_handlers[msg_type] = handler;
}

static void unregister_msg_handler(int msg_type)
{
	if(msg_type > 3 || msg_type < 1) {
		return;
	}
	msg_handlers[msg_type] = NULL;
}

static unsigned long add_payload(size_t len) 
{
	unsigned long payload_addr;
	unsigned long offset;

	payload_addr = gen_pool_alloc(ringbuf_dev.payload_pool, len);
	offset = payload_addr - (unsigned long)ringbuf_dev.payload_area;

	// printk(KERN_INFO "alloc payload memory at offset: %lu\n", offset);
	return offset;
}

static void free_payload(rbmsg_hd *hd)
{
	gen_pool_free(ringbuf_dev.payload_pool, 
			(unsigned long)ringbuf_dev.payload_area + hd->payload_off,
			hd->payload_len);
	// printk(KERN_INFO "free payload memory at offset: %lu\n", node->offset);
}

static void recv_msg(struct tasklet_struct* data) 
{	
	msg_handler handler = NULL;
	rbmsg_hd hd;
	size_t ret_len;

	fifo *fifo_addr;
	if(ringbuf_dev.role == Host) {
		fifo_addr = ringbuf_dev.fifo_guest_addr;
	} else {
		fifo_addr = ringbuf_dev.fifo_host_addr;
	}

	if(kfifo_len(fifo_addr) < MSG_SZ) {
		printk(KERN_ERR "no msg in ring buffer\n");
		return;
	}

	fifo_addr->kfifo.data = (void*)fifo_addr + 0x18;
	mb();
	ret_len = kfifo_out(fifo_addr, (char*)&hd, MSG_SZ);
		
	if(hd.src_qid != QEMU_PROCESS_ID) {
		printk(KERN_ERR "invalid ring buffer msg\n");
		return;
	} else {
		handler = msg_handlers[hd.msg_type];
		handler(&hd);
	}
}

static unsigned int send_msg(fifo *fifo_addr, rbmsg_hd* hd, void *notify_addr) 
{
	if(kfifo_avail(fifo_addr) < MSG_SZ) {
		printk(KERN_ERR "not enough space in ring buffer\n");
		return -1;
	}

	fifo_addr->kfifo.data = (void*)fifo_addr + 0x18;
	rmb();
	kfifo_in(fifo_addr, (char*)hd, MSG_SZ);

	ringbuf_notify(notify_addr);
	return 0;
}

// static ssize_t ringbuf_write(struct file * filp, const char * buffer, 
// 					size_t len, loff_t *offset)
// {
// 	return 0;
// }

// static ssize_t ringbuf_read(struct file * filp, char * buffer, size_t len, 
// 							loff_t *offset)
// {
// 	return 0;
// }

static void ringbuf_fifo_init(void) 
{
	fifo fifo_indevice;
	ringbuf_device *dev = &ringbuf_dev;

	if(dev->role == Host) {
		dev->payload_pool = gen_pool_create(0, -1);
		if(gen_pool_add(dev->payload_pool, (unsigned long)(dev->payload_area),
									3 << 20, -1)) {
			printk(KERN_INFO "gen_pool create failed!!!!!");
			return;
		}
	}

	if(kfifo_size(dev->fifo_host_addr) != RINGBUF_SZ) {
		memcpy(dev->fifo_host_addr, &fifo_indevice, 
					sizeof(fifo_indevice));
		INIT_KFIFO(*(dev->fifo_host_addr));
		
		*(dev->notify_guest_addr) = 0;
	}

	if(kfifo_size(dev->fifo_guest_addr) != RINGBUF_SZ) {
		memcpy(dev->fifo_guest_addr, &fifo_indevice, 
					sizeof(fifo_indevice));
		INIT_KFIFO(*(dev->fifo_guest_addr));

		*(dev->notify_host_addr) = 0;
	}
	
	poll_workqueue = create_workqueue("poll_workqueue");
	queue_work(poll_workqueue, &poll_work);

	register_msg_handler(msg_type_req, handle_msg_type_req);
	register_msg_handler(msg_type_add, handle_msg_type_add);
	register_msg_handler(msg_type_free, handle_msg_type_free);
}

static int ringbuf_open(struct inode * inode, struct file * filp)
{

	printk(KERN_INFO "Opening ringbuf device\n");

	if (MINOR(inode->i_rdev) != RINGBUF_DEVICE_MINOR_NR) {
		printk(KERN_INFO "minor number is %d\n", 
				RINGBUF_DEVICE_MINOR_NR);
		return -ENODEV;
	}
	filp->private_data = (void*)(&ringbuf_dev);
	ringbuf_dev.minor = RINGBUF_DEVICE_MINOR_NR;

   return 0;
}

static int ringbuf_release(struct inode * inode, struct file * filp)
{
	if(ringbuf_dev.fifo_host_addr != NULL) {
		printk(KERN_INFO "ring buffer is being freed");
		kfifo_free(ringbuf_dev.fifo_host_addr);
	}

	printk(KERN_INFO "release ringbuf_device\n");

   	return 0;
}

static int ringbuf_probe_device (struct pci_dev *pdev,
				const struct pci_device_id * ent) 
{
	int ret;
	struct ringbuf_device *dev = &ringbuf_dev;
	printk(KERN_INFO "probing for device: %s\n", (pci_name(pdev)));
	if((int)pci_name(pdev)[9] - 48 != PCIID)
		return -1;

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

	pci_read_config_byte(pdev, PCI_REVISION_ID, &(dev->revision));

	printk(KERN_INFO "device %d:%d, revision: %d\n", device_major_nr,
		dev->minor, dev->revision);

	/* Pysical address of BAR0, BAR1, BAR2 */
	dev->bar0_addr = pci_resource_start(pdev, 0);
	dev->bar0_size = pci_resource_len(pdev, 0);
	dev->bar1_addr = pci_resource_start(pdev, 1);
	dev->bar1_size = pci_resource_len(pdev, 1);
	dev->bar2_addr = pci_resource_start(pdev, 2);
	dev->bar2_size = pci_resource_len(pdev, 2);

	dev->dev = pdev;
	dev->role = ROLE;

	dev->regs_addr = ioremap(dev->bar0_addr, dev->bar0_size);
	if (!dev->regs_addr) {
		printk(KERN_INFO "unable to ioremap bar0, sz: %d\n", 
						dev->bar0_size);
		goto release_regions;
	}

	dev->base_addr = ioremap(dev->bar2_addr, dev->bar2_size);
	if (!dev->base_addr) {
		printk(KERN_INFO "unable to ioremap bar2, sz: %d\n", 
						dev->bar2_size);
		goto iounmap_bar0;
	}

	ringbuf_dev.fifo_host_addr = (fifo*)ringbuf_dev.base_addr;
	ringbuf_dev.fifo_guest_addr = (fifo*)(ringbuf_dev.base_addr + sizeof(fifo));

	ringbuf_dev.notify_guest_addr = 
		(unsigned int *)(ringbuf_dev.base_addr + 2 * sizeof(fifo));
	ringbuf_dev.notify_host_addr = ringbuf_dev.notify_guest_addr + 1;
	
	ringbuf_dev.payload_area = 
		ringbuf_dev.base_addr + 2 * sizeof(fifo) 
		+ 2 * sizeof(*ringbuf_dev.notify_guest_addr);	

	ringbuf_fifo_init();

	printk(KERN_INFO "device probed\n");
	return 0;

iounmap_bar0:
    	iounmap(dev->regs_addr);

release_regions:
    	dev->dev = NULL;
    	pci_release_regions(pdev);

disable_device:
    	pci_disable_device(pdev);

out:
    	return ret;
}

static void ringbuf_remove_device(struct pci_dev* pdev)
{
	struct ringbuf_device *dev = &ringbuf_dev;

	printk(KERN_INFO "removing ivshmem device\n");
	
	//TODO: free the payloads pool!!!!!!!!!!!!!!
	dev->dev = NULL;

	iounmap(dev->regs_addr);
	iounmap(dev->base_addr);

	pci_release_regions(pdev);
	pci_disable_device(pdev);

	destroy_workqueue(poll_workqueue);
	unregister_msg_handler(msg_type_req);
	unregister_msg_handler(msg_type_add);
	unregister_msg_handler(msg_type_free);
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

module_init(ringbuf_init);
module_exit(ringbuf_cleanup);