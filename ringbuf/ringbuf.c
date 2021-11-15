/*
 * drivers/char/ringbuf_ivshmem.c - driver of ring buffer based on Inter-VM shared memory PCI device
 *
 * Xiangyu Ren <180110718@mail.hit.edu.cn>
 *
 * Based on kvm_ivshmem.c:
 *         Copyright 2009 Cam Macdonell
 * 
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
// #include <linux/smp_lock.h>
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
#define RINGBUF_DEV_ROLE Consumer
#define BUF_INFO_SZ sizeof(ringbuf_info)
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

enum {
	/* KVM Inter-VM shared memory device register offsets */
	IntrMask        = 0x00,    /* Interrupt Mask */
	IntrStatus      = 0x04,    /* Interrupt Status */
	IVPosition      = 0x08,    /* VM ID */
	Doorbell        = 0x0c,    /* Doorbell */
};

enum {
	/* Consumer(read) or Producer(write) role of ring buffer*/
	Consumer	= 	0,
	Producer	=	1,
};



/*START--------------------------------------- ringbuf device and its file operations */
// the read/write pointer of ring buffer
typedef struct ringbuf_info
{
	unsigned int in;
	unsigned int out;
	unsigned int size;
	void * buf_addr;
} ringbuf_info;

typedef struct ringbuf_device
{
	struct pci_dev* dev;
	void __iomem* regs;

	void * base_addr;

	unsigned int regaddr;
	unsigned int reg_size;

	unsigned int ioaddr;
	unsigned int ioaddr_size;
	unsigned int irq;
	unsigned int bufsize;
	
	unsigned int role;
	unsigned int enabled;
} ringbuf_device;


static int __init ringbuf_init(void);
static void __exit ringbuf_cleanup(void);
// static int ringbuf_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
//static int ringbuf_mmap(struct file *, struct vm_area_struct *);
static int ringbuf_open(struct inode *, struct file *);
static int ringbuf_release(struct inode *, struct file *);
static ssize_t ringbuf_read(struct file *, char *, size_t, loff_t *);
static ssize_t ringbuf_write(struct file *, const char *, size_t, loff_t *);
//static loff_t ringbuf_lseek(struct file * filp, loff_t offset, int origin);

static ringbuf_device ringbuf_dev;
// static int event_num;
// static struct semaphore sema;
// static wait_queue_head_t wait_queue;

static int device_major_nr;
static const struct file_operations ringbuf_ops = {
	.owner		= 	THIS_MODULE,
	.open		= 	ringbuf_open,
	//.mmap		= 	ringbuf_mmap,
	.read		= 	ringbuf_read,
	// .ioctl   	= 	ringbuf_ioctl,
	.write   	= 	ringbuf_write,
	//.llseek  	=	ringbuf_lseek,
	.release 	= 	ringbuf_release,
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
	ringbuf_info buf_info;
	unsigned int bytes_uncopied = 0;
	unsigned int space = 0;

	/* if the device role is not Consumer, than not allowed to read */
	if(ringbuf_dev.role != Consumer) {
		printk(KERN_ERR "ringbuf: not allowed to read \n");
		return 0;
	}
	if (!ringbuf_dev.base_addr) {
		printk(KERN_ERR "ringbuf: cannot read from ioaddr (NULL)\n");
		return 0;
	}
	// update the position of in pointer
	// buf_info = (ringbuf_info*)ringbuf_dev.base_addr;
	memcpy(&buf_info, ringbuf_dev.base_addr, BUF_INFO_SZ);

	/*----------------- Start to copy ---------------------*/
	/* out - in, the largest length for reading */
	len = MIN(len, (buf_info.out - buf_info.in));
	if(len == 0) 
		return 0;

	/* sz - (out % sz), the largest space for a single time of read */
	space = MIN(len, (RINGBUF_SZ - (buf_info.out & (RINGBUF_SZ - 1))));

	// copy first part of the data to user buffer
	bytes_uncopied = copy_to_user(
		buffer, 
		buf_info.buf_addr + (buf_info.out & (RINGBUF_SZ - 1)), 
		space);

	//if the copy is incomplete	
	if(bytes_uncopied > 0) {
		// buf_info.out += (space - bytes_uncopied);
		goto copy_err;
	}

	/*-----------optional: second part of copy--------------*/
	if((len - space) > 0) {
		// copy second part of the data to user buffer
		bytes_uncopied = copy_to_user(
			buffer + space, 
			buf_info.buf_addr, 
			len - space);
		
		//if the copy is incomplete	
		if(bytes_uncopied > 0) {
			// buf_info.out += (len - bytes_uncopied);
			goto copy_err;
		}
	}

	/*-------------- finishing the copy---------------------*/
	buf_info.out += len;
	memcpy(ringbuf_dev.base_addr, &buf_info, BUF_INFO_SZ);
	return len;

	/* handle error, if the copy is incomplete*/
copy_err:
	memcpy(ringbuf_dev.base_addr, &buf_info, BUF_INFO_SZ);
	return -EFAULT;
}

static ssize_t ringbuf_write(struct file * filp, const char * buffer, size_t len, loff_t *offset)
{

	ringbuf_info buf_info;
	unsigned int bytes_uncopied = 0;
	unsigned int space = 0;

	/* if the device role is not Consumer, than not allowed to read */
	if(ringbuf_dev.role != Producer) {
		printk(KERN_ERR "ringbuf: not allowed to write \n");
		return 0;
	}
	if (!ringbuf_dev.base_addr) {
		printk(KERN_ERR "ringbuf: cannot write from ioaddr (NULL)\n");
		return 0;
	}
	// update the position of out pointer
	// buf_info = (ringbuf_info*)ringbuf_dev.base_addr;
	memcpy(&buf_info, ringbuf_dev.base_addr, RINGBUF_SZ);

	/*----------------- Start to copy ---------------------*/
	/* sz - (in - out), the largest length for writing */
	len = MIN(len, RINGBUF_SZ - (buf_info.in - buf_info.out));
	if(len == 0) 
		return 0;

	/* sz - (in % sz), the largest space for a single time of write */
	space = MIN(len, (RINGBUF_SZ - (buf_info.in & (RINGBUF_SZ - 1))));

	// copy first part of the data from user buffer
	bytes_uncopied = copy_from_user( 
		buf_info.buf_addr + (buf_info.in & (RINGBUF_SZ - 1)), 
		buffer,
		space);

	//if the copy is incomplete	
	if(bytes_uncopied > 0) {
		// buf_info.in += (space - bytes_uncopied);
		goto copy_err;
	}

	/*-----------optional: second part of copy--------------*/
	if((len - space) > 0) {
		// copy second part of the data from user buffer
		bytes_uncopied = copy_to_user( 
			buf_info.buf_addr, 
			buffer + space,
			len - space);
		
		//if the copy is incomplete	
		if(bytes_uncopied > 0) {
			// buf_info.in += (len - bytes_uncopied);
			goto copy_err;
		}
	}

	/*-------------- finishing the copy---------------------*/
	buf_info.in += len;
	memcpy(ringbuf_dev.base_addr, &buf_info, RINGBUF_SZ);
	return len;

	/* handle error, if the copy is incomplete*/
copy_err:
	memcpy(ringbuf_dev.base_addr, &buf_info, RINGBUF_SZ);
	return -EFAULT;
}


static int ringbuf_open(struct inode * inode, struct file * filp)
{

   printk(KERN_INFO "Opening ringbuf device\n");

   if (MINOR(inode->i_rdev) != RINGBUF_DEVICE_MINOR_NR) {
	  printk(KERN_INFO "minor number is %d\n", RINGBUF_DEVICE_MINOR_NR);
	  return -ENODEV;
   }

   return 0;
}

static int ringbuf_release(struct inode * inode, struct file * filp)
{

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

	memcpy(&buf_info, ringbuf_dev.base_addr, BUF_INFO_SZ);
	
	if(buf_info.size != BUF_INFO_SZ) {
		printk(KERN_INFO "RINGBUF at %x buffer not initialized yet. Start initialization...\n", ringbuf_dev.ioaddr);
		buf_info.in = buf_info.out = 0;
		buf_info.buf_addr = ringbuf_dev.base_addr + BUF_INFO_SZ;
		buf_info.size = BUF_INFO_SZ;
		memcpy(ringbuf_dev.base_addr, &buf_info, BUF_INFO_SZ);
	}


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