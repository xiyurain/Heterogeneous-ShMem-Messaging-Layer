/*
 * drivers/char/ringbuf_ivshmem.c - driver of ring buffer based on Inter-VM shared memory PCI device
 *
 * Xiangyu Ren <180110718@mail.hit.edu.cn>
 *
 * Based on kvm_ivshmem.c:
 *         Copyright 2009 Cam Macdonell
 * 
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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Xiangyu Ren <180110718@mail.hit.edu.cn>");
MODULE_DESCRIPTION("ring buffer based on Inter-VM shared memory module");
MODULE_VERSION("1.0");

#define RINGBUF_SZ 4096
#define TRUE 1
#define FALSE 0
#define RINGBUF_DEVICE_MINOR_NR 0
#define RINGBUF_DEV_ROLE Producer
#define BUF_INFO_SZ sizeof(ringbuf_info)
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

/* KVM Inter-VM shared memory device register offsets */
enum {
	IntrMask        = 0x00,    /* Interrupt Mask */
	IntrStatus      = 0x04,    /* Interrupt Status */
	IVPosition      = 0x08,    /* VM ID */
	Doorbell        = 0x0c,    /* Doorbell */
};

/* Consumer(read) or Producer(write) role of ring buffer*/
enum {
	Consumer	= 	0,
	Producer	=	1,
};



/*START--------------------------------------- ringbuf device and its file operations */

/*
 * @base_addr: mapped start address of IVshmem space
 * @regaddr: physical address of shmem PCIe dev regs
 * @ioaddr: physical address of IVshmem IO space
 * @fifo_addr: address of the Kfifo struct
 * @payloads_list: a linklist of the msg payloads
*/
typedef struct ringbuf_device
{
	struct pci_dev *dev;
	void __iomem *egs;

	void* base_addr;

	unsigned int regaddr;
	unsigned int reg_size;

	unsigned int ioaddr;
	unsigned int ioaddr_size;
	unsigned int irq;

	kfifo* fifo_addr;
	unsigned int bufsize;

	void* payloads_list;
	
	unsigned int role;
	unsigned int enabled;
} ringbuf_device;


static int __init ringbuf_init(void);
static void __exit ringbuf_cleanup(void);
static int ringbuf_open(struct inode *, struct file *);
static int ringbuf_release(struct inode *, struct file *);
static ssize_t ringbuf_read(struct file *, char *, size_t, loff_t *);
static ssize_t ringbuf_write(struct file *, const char *, size_t, loff_t *);
// static int ringbuf_ioctl(struct inode *, struct file *, unsigned int, unsigned long);

static ringbuf_device ringbuf_dev;
// static int event_num;
// static struct semaphore sema;
// static wait_queue_head_t wait_queue;

static int device_major_nr;
static const struct file_operations ringbuf_ops = {
	.owner		= 	THIS_MODULE,
	.open		= 	ringbuf_open,
	.read		= 	ringbuf_read,
	.write   	= 	ringbuf_write,
	.release 	= 	ringbuf_release,
	// .ioctl   	= 	ringbuf_ioctl,
};
/*end-------------ringbuf device and its file operations----------*/


/*start---------------------- Inter-VM shared memory PCI device------- */
static struct pci_device_id ringbuf_id_table[] = {
	{ 0x1af4, 0x1110, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0 },
};
MODULE_DEVICE_TABLE(pci, ringbuf_id_table);

static void ringbuf_remove_device(struct pci_dev* pdev);
static int ringbuf_probe_device(struct pci_dev *pdev,
						const struct pci_device_id * ent);

static struct pci_driver ringbuf_pci_driver = {
	.name		= "ringbuf",
	.id_table	= ringbuf_id_table,
	.probe	   = ringbuf_probe_device,
	.remove	  = ringbuf_remove_device,
};
/*end-------------Inter-VM shared memory PCI device---------------*/



/*START---------------------Implementation of ringbuf_device------------------*/
// static int ringbuf_ioctl(struct inode * ino, struct file * filp,
// 			unsigned int cmd, unsigned long arg)
// {
// 	int rv;
// 	uint32_t msg;

// 	printk("RINGBUF: args is %ld\n", arg);
// #if 0
// 	switch (cmd) {

// 			printk("RINGBUF: bad ioctl (\n");
// 	}
// #endif
// 	return 0;
// }

static ssize_t ringbuf_read(struct file * filp, char * buffer, size_t len, loff_t *offset)
{
	
}

static ssize_t ringbuf_write(struct file * filp, const char * buffer, size_t len, loff_t *offset)
{

	
}


static int ringbuf_open(struct inode * inode, struct file * filp)
{

   printk(KERN_INFO "Opening ringbuf device\n");

   if (MINOR(inode->i_rdev) != RINGBUF_DEVICE_MINOR_NR) {
	  printk(KERN_INFO "minor number is %d\n", RINGBUF_DEVICE_MINOR_NR);
	  return -ENODEV;
   }

	printk(KERN_INFO "Check if the ring buffer is already init");
	if(kfifo_esize(ringbuf_dev.fifo_addr) != RINGBUF_SZ) {
		
		printk(KERN_INFO "Start to init the ring buffer\n");
		kfifo_alloc(ringbuf_dev, RINGBUF_SZ, GFP_KERNEL);

	}

	printk(KERN_INFO "Check if the payloads linklist is already init");
	// TODO: init with the list struct

   return 0;
}

static int ringbuf_release(struct inode * inode, struct file * filp)
{
	if(ringbuf_dev.kfifo_addr != NULL) {
		//TODO: free the payloads linklist

		printk(KERN_INFO "ring buffer is being freed");
		kfifo_free(ringbuf_dev.fifo_addr);
	}

   return 0;
}
/*END---------------------Implementation of ringbuf_device------------------*/


/*START---------------------Implementation of pci_device------------------*/
static int ringbuf_probe_device (struct pci_dev *pdev,
					const struct pci_device_id * ent) {

	int result;
	ringbuf_info buf_info;

	printk("RINGBUF: Probing for ringbuf Device\n");

	result = pci_enable_device(pdev);
	if (result) {
		printk(KERN_ERR "Cannot probe RINGBUF device %s: error %d\n",
		pci_name(pdev), result);
		return result;
	}

	result = pci_request_regions(pdev, "ringbuf");
	if (result < 0) {
		printk(KERN_ERR "RINGBUF: cannot request regions\n");
		goto pci_disable;
	} else printk(KERN_ERR "RINGBUF: result is %d\n", result);

	ringbuf_dev.ioaddr = pci_resource_start(pdev, 2);
	ringbuf_dev.ioaddr_size = pci_resource_len(pdev, 2);

	ringbuf_dev.base_addr = pci_iomap(pdev, 2, 0);
	printk(KERN_INFO "RINGBUF: iomap base = 0x%lu \n",
							(unsigned long) ringbuf_dev.base_addr);

	if (!ringbuf_dev.base_addr) {
		printk(KERN_ERR "RINGBUF: cannot iomap region of size %d\n",
							ringbuf_dev.ioaddr_size);
		goto pci_release;
	}

	printk(KERN_INFO "RINGBUF: ioaddr = %x ioaddr_size = %d\n",
						ringbuf_dev.ioaddr, ringbuf_dev.ioaddr_size);

	/* The part of BAR0 and BAR1 TODO

	ringbuf_dev.regaddr =  pci_resource_start(pdev, 0);
	ringbuf_dev.reg_size = pci_resource_len(pdev, 0);
	ringbuf_dev.regs = pci_iomap(pdev, 0, 0x100);

	ringbuf_dev.dev = pdev;

	if (!ringbuf_dev.regs) {
		printk(KERN_ERR "KVM_IVSHMEM: cannot ioremap registers of size %d\n",
							ringbuf_dev.reg_size);
		goto reg_release;
	}

	// set all masks to on 
	writel(0xffffffff, ringbuf_dev.regs + IntrMask);

	// by default initialize semaphore to 
	sema_init(&sema, 0);

	init_waitqueue_head(&wait_queue);
	event_num = 0;

	if (request_msix_vectors(&ringbuf_dev, 4) != 0) {
		printk(KERN_INFO "regular IRQs\n");
		if (request_irq(pdev->irq, ringbuf_interrupt, IRQF_SHARED,
							"ringbuf", &ringbuf_dev)) {
			printk(KERN_ERR "KVM_IVSHMEM: cannot get interrupt %d\n", pdev->irq);
			printk(KERN_INFO "KVM_IVSHMEM: irq = %u regaddr = %x reg_size = %d\n",
					pdev->irq, ringbuf_dev.regaddr, ringbuf_dev.reg_size);
		}
	} else {
		printk(KERN_INFO "MSI-X enabled\n");
	}
	*/

	return 0;

// reg_release:
// 	pci_iounmap(pdev, ringbuf_dev.base_addr);
pci_release:
	pci_release_regions(pdev);
pci_disable:
	pci_disable_device(pdev);
	return -EBUSY;
}

static void ringbuf_remove_device(struct pci_dev* pdev)
{
	printk(KERN_INFO "Unregister ringbuf device.\n");
	// free_irq(pdev->irq,&ringbuf_dev);
	// pci_iounmap(pdev, ringbuf_dev.regs);
	pci_iounmap(pdev, ringbuf_dev.base_addr);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}
/*END---------------------Implementation of pci_device------------------*/



static void __exit ringbuf_cleanup(void)
{
	pci_unregister_driver(&ringbuf_pci_driver);
	unregister_chrdev(device_major_nr, "ringbuf");
}

static int __init ringbuf_init (void)
{

	int err = -ENOMEM;

	/* Register device node ops. */
	err = register_chrdev(0, "ringbuf", &ringbuf_ops);
	if (err < 0) {
		printk(KERN_ERR "Unable to register ringbuf device\n");
		return err;
	}
	device_major_nr = err;
	printk("RINGBUF: Major device number is: %d\n", device_major_nr);
	ringbuf_dev.enabled=FALSE;

	err = pci_register_driver(&ringbuf_pci_driver);
	if (err < 0) {
		goto error;
	}

	return 0;

error:
	unregister_chrdev(device_major_nr, "ringbuf");
	return err;
}

module_init(ringbuf_init);
module_exit(ringbuf_cleanup);