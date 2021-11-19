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
#define RINGBUF_MSG_SZ sizeof(rbmsg)
#define BUF_INFO_SZ sizeof(ringbuf_info)
#define TRUE 1
#define FALSE 0
#define RINGBUF_DEVICE_MINOR_NR 0
#define RINGBUF_DEV_ROLE Producer
#define QEMU_PROCESS_ID 1
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

/*
 * message sent via ring buffer, as header of the payloads
*/
typedef struct ringbuf_msg_hd
{
	unsigned int src_qid;

	unsigned int payload_off;
	ssize_t payload_len;
} rbmsg_hd;

/*--------------------------------------- ringbuf device and its file operations */

/*
 * @base_addr: mapped start address of IVshmem space
 * @regaddr: physical address of shmem PCIe dev regs
 * @ioaddr: physical address of IVshmem IO space
 * @fifo_addr: address of the Kfifo struct
 * @payloads_st: start address of the payloads area
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

	void* payloads_st;
	
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
static unsigned int payload_pt;

static int event_num;
static struct semaphore sema;
static wait_queue_head_t wait_queue;

static int device_major_nr;

enum ivshmem_ioctl { set_sema, down_sema, empty, wait_event, wait_event_irq, read_ivposn, read_livelist, sema_irq };

static const struct file_operations ringbuf_ops = {
	.owner		= 	THIS_MODULE,
	.open		= 	ringbuf_open,
	.read		= 	ringbuf_read,
	.write   	= 	ringbuf_write,
	.release 	= 	ringbuf_release,
	// .ioctl   	= 	ringbuf_ioctl,
};


/*---------------------- Inter-VM shared memory PCI device------- */
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



/*---------------------Implementation of ringbuf_device------------------*/
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


static irqreturn_t ringbuf_interrupt (int irq, void *dev_instance)
{
	struct ringbuf_device * dev = dev_instance;
	u32 status;

	if (unlikely(dev == NULL))
		return IRQ_NONE;

	status = readl(dev->regs + IntrStatus);
	if (!status || (status == 0xFFFFFFFF))
		return IRQ_NONE;

	if (status == sema_irq) {
		up(&sema);
	} else if (status == wait_event_irq) {
		event_num = 1;
		wake_up_interruptible(&wait_queue);
	}

	printk(KERN_INFO "RINGBUF: interrupt (status = 0x%04x)\n",
		   status);

	return IRQ_HANDLED;
}

static ssize_t ringbuf_read(struct file * filp, char * buffer, size_t len, loff_t *offset)
{
	rbmsg_hd hd;
	unsigned int msgread_len;

	/* if the device role is not Consumer, than not allowed to read */
	if(ringbuf_dev.role != Consumer) {
		printk(KERN_ERR "ringbuf: not allowed to read \n");
		return 0;
	}
	if(!ringbuf_dev.base_addr || !ringbuf_dev.fifo_addr) {
		printk(KERN_ERR "ringbuf: cannot read from addr (NULL)\n");
		return 0;
	}
	if(kfifo_len(ringbuf_dev.fifo_addr) < RINGBUF_MSG_SZ) {
		printk(KERN_ERR "no msg in ring buffer\n");
		return 0;
	}

	msgread_len = kfifo_out(ringbuf_dev.fifo_addr, &hd, RINGBUF_MSG_SZ);
	if(hd.src_qid != QEMU_PROCESS_ID) {
		printk(KERN_ERR "invalid ring buffer msg\n");
		goto err;
	}

	rmb();

	memcpy(buffer, ringbuf_dev.payload_st + hd.payload_off, 
			MIN(len, hd.payload_len));
	return 0;

err:
	return -EFAULT;
}

static ssize_t ringbuf_write(struct file * filp, const char * buffer, size_t len, loff_t *offset)
{
	rbmsg_hd hd;
	unsigned int msgsent_len;

	if(ringbuf_dev.role != Producer) {
		printk(KERN_ERR "ringbuf: not allowed to write \n");
		return 0;
	}
	if(!ringbuf_dev.base_addr || !ringbuf_dev.fifo_addr) {
		printk(KERN_ERR "ringbuf: cannot read from addr (NULL)\n");
		return 0;
	}
	if(kfifo_avail(ringbuf_dev.fifo_addr) < RINGBUF_MSG_SZ) {
		printk(KERN_ERR "not enough space in ring buffer\n");
		return 0;
	}

	hd.src_qid = QEMU_PROCESS_ID;
	hd.payload_off = payload_pt;
	hd.payload_len = len;
	
	memcpy(ringbuf_dev.payloads_st + hd.payload_off, buffer, len);

	wmb();

	msgsent_len = kfifo_in(ringbuf_dev.fifo_addr, &hd, RINGBUF_MSG_SZ);
	if(msgsent_len != RINGBUF_MSG_SZ) {
		printk(KERN_ERR "ring buffer msg incomplete! only %d sent\n", msgsent_len);
		goto err;
	}

	payload_pt += len;
	return 0;

err:
	return -EFAULT;
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
	payload_pt = 0;

   return 0;
}

static int ringbuf_release(struct inode * inode, struct file * filp)
{
	if(ringbuf_dev.kfifo_addr != NULL) {
		//TODO: free the payloads linklist
		payload_pt = 0;
		printk(KERN_INFO "ring buffer is being freed");
		kfifo_free(ringbuf_dev.fifo_addr);
	}

   return 0;
}


/*---------------------Implementation of pci_device------------------*/

static int request_msix_vectors(struct kvm_ivshmem_device *ivs_info, int nvectors)
{
	int i, err;
	const char *name = "ivshmem";

	printk(KERN_INFO "devname is %s\n", name);
	ivs_info->nvectors = nvectors;


	ivs_info->msix_entries = kmalloc(nvectors * sizeof *ivs_info->msix_entries,
					   GFP_KERNEL);
	ivs_info->msix_names = kmalloc(nvectors * sizeof *ivs_info->msix_names,
					 GFP_KERNEL);

	for (i = 0; i < nvectors; ++i)
		ivs_info->msix_entries[i].entry = i;

	err = pci_enable_msix(ivs_info->dev, ivs_info->msix_entries,
					ivs_info->nvectors);
	if (err > 0) {
		printk(KERN_INFO "no MSI. Back to INTx.\n");
		return -ENOSPC;
	}

	if (err) {
		printk(KERN_INFO "some error below zero %d\n", err);
		return err;
	}

	for (i = 0; i < nvectors; i++) {

		snprintf(ivs_info->msix_names[i], sizeof *ivs_info->msix_names,
		 "%s-config", name);

		err = request_irq(ivs_info->msix_entries[i].vector,
				  kvm_ivshmem_interrupt, 0,
				  ivs_info->msix_names[i], ivs_info);

		if (err) {
			printk(KERN_INFO "couldn't allocate irq for msi-x entry %d with vector %d\n", i, ivs_info->msix_entries[i].vector);
			return -ENOSPC;
		}
	}

	return 0;
}

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

	ringbuf_dev.fifo_addr = (kfifo*)ringbuf_dev.base_addr;
	ringbuf_dev.payloads_st = ringbuf_dev.base_addr + sizeof(kfifo);	

	/* The part of BAR0 and BAR1 */

	ringbuf_dev.regaddr =  pci_resource_start(pdev, 0);
	ringbuf_dev.reg_size = pci_resource_len(pdev, 0);
	ringbuf_dev.regs = pci_iomap(pdev, 0, 0x100);

	ringbuf_dev.dev = pdev;

	if (!ringbuf_dev.regs) {
		printk(KERN_ERR "KVM_IVSHMEM: cannot ioremap registers of size %d\n",
							ringbuf_dev.reg_size);
		goto reg_release;
	}

	writel(0xffffffff, ringbuf_dev.regs + IntrMask);

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