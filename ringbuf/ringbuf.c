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
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pci_regs.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/wait.h>

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

#define IOCTL_MAGIC				('f')
#define IOCTL_RING				_IOW(IOCTL_MAGIC, 1, u32)
#define IOCTL_WAIT				_IO(IOCTL_MAGIC, 2)
#define IOCTL_IVPOSITION		_IOR(IOCTL_MAGIC, 3, u32)
#define IVPOSITION_REG_OFF		0x08
#define DOORBELL_REG_OFF		0x0c

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
	struct pci_dev 		*dev;
	int 				minor;

	u8 					revision;
	unsigned int 		ivposition;

	void __iomem 		*regs;
	void __iomem 		*base_addr;

	unsigned int 		bar0_addr;
	unsigned int 		bar0_size;
	unsigned int 		bar1_addr;
    unsigned int 		bar1_size;

	char                (*msix_names)[256];
    struct msix_entry   *msix_entries;
	int                 nvectors;

	unsigned int 		bar2_addr;
	unsigned int 		bar2_size;
	// unsigned int 	irq;

	kfifo* 				fifo_addr;
	unsigned int 		bufsize;
	void* 				payloads_st;
	
	unsigned int 		role;
	unsigned int 		enabled;
} ringbuf_device;


static int __init ringbuf_init(void);
static void __exit ringbuf_cleanup(void);
static int ringbuf_open(struct inode *, struct file *);
static int ringbuf_release(struct inode *, struct file *);
static ssize_t ringbuf_read(struct file *, char *, size_t, loff_t *);
static ssize_t ringbuf_write(struct file *, const char *, size_t, loff_t *);
// static int ringbuf_ioctl(struct inode *, struct file *, unsigned int, unsigned long);

static int event_toggle;
DECLARE_WAIT_QUEUE_HEAD(wait_queue);

static ringbuf_device ringbuf_dev;
static unsigned int payload_pt;
static int device_major_nr;


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
static long ringbuf_ioctl(struct file *filp, unsigned int cmd, unsigned int value)
{
    int ret;
    u16 ivposition;
    u16 vector;

	ringbuf_device *dev = &ringbuf_dev;
    BUG_ON(dev->base_addr == NULL);

    switch (cmd) {
    case IOCTL_RING:
        vector = value & 0xffff;
        ivposition = value & 0xffff0000 >> 16;
        printk(KERN_INFO "ring bell: value: %u(0x%x), vector: %u, peer id: %u\n",
                value, value, vector, ivposition);
        writel(value & 0xffffffff, dev->regs_addr + DOORBELL_OFF);
        break;

    case IOCTL_WAIT:
        printk(KERN_INFO "wait for interrupt\n");
        ret = wait_event_interruptible(wait_queue, (event_toggle == 1));
        if (ret == 0) {
            printk(KERN_INFO "wakeup\n");
            event_toggle = 0;
        } else if (ret == -ERESTARTSYS) {
            printk(KERN_INFO  "interrupted by signal\n");
            return ret;
        } else {
            printk(KERN_INFO "unknown failed: %d\n", ret);
            return ret;
        }
        break;

    case IOCTL_IVPOSITION:
        printk(KERN_INFO "get ivposition: %u\n", dev->ivposition);
        return dev->ivposition;

    default:
        printk(KERN_INFO "bad ioctl command: %d\n", cmd);
        return -1;
    }

    return 0;
}


static irqreturn_t ringbuf_interrupt (int irq, void *dev_instance)
{
	struct ringbuf_device * dev = dev_instance;

	if (unlikely(dev == NULL))
		return IRQ_NONE;

	printk(KERN_INFO "RINGBUF: interrupt: %d\n", irq);

	event_toggle = 1;
	wake_up_interruptible(&wait_queue);

	return IRQ_HANDLED;
}



static int request_msix_vectors(struct ringbuf_device *dev, int n)
{
    int ret, i;
    ret = -EINVAL;

    printk(KERN_INFO "request msi-x vectors: %d\n", n);

    dev->nvectors = n;

    dev->msix_entries = kmalloc(n * sizeof(struct msix_entry), GFP_KERNEL);
    if (dev->msix_entries == NULL) {
        ret = -ENOMEM;
        goto error;
    }

    dev->msix_names = kmalloc(n * sizeof(*dev->msix_names), GFP_KERNEL);
    if (dev->msix_names == NULL) {
        ret = -ENOMEM;
        goto free_entries;
    }

    for (i = 0; i < n; i++)
        dev->msix_entries[i].entry = i;

    ret = pci_enable_msix_exact(dev->dev, dev->msix_entries, n);
    if (ret) {
        printk(KERN_ERR "unable to enable msix: %d\n", ret);
        goto free_names;
    }

    for (i = 0; i < dev->nvectors; i++) {
        snprintf(dev->msix_names[i], sizeof(*dev->msix_names),
                "%s%d-%d", DRV_NAME, dev->minor, i);

        ret = request_irq(dev->msix_entries[i].vector,
                ringbuf_interrupt, 0, dev->msix_names[i], dev);

        if (ret) {
            printk(KERN_ERR "unable to alloc irq for msixentry %d vec %d\n",
			 		i,dev->msix_entries[i].vector);
            goto release_irqs;
        }

        printk(KERN_INFO "irq for msix entry: %d, vector: %d\n",
                i, dev->msix_entries[i].vector);
    }

    return 0;

release_irqs:
    for ( ; i > 0; i--) {
        free_irq(dev->msix_entries[i-1].vector, dev);
    }
    pci_disable_msix(dev->dev);

free_names:
    kfree(dev->msix_names);

free_entries:
    kfree(dev->msix_entries);

error:
    return ret;
}



static void free_msix_vectors(struct ringbuf_device *dev)
{
    int i;
    for (i = dev->nvectors; i > 0; i--) {
        free_irq(dev->msix_entries[i-1].vector, dev);
    }
    pci_disable_msix(dev->dev);
    kfree(dev->msix_names);
    kfree(dev->msix_entries);
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
	filp->private_data = (void*) ringbuf_dev;
	ringbuf_dev.minor = RINGBUF_DEVICE_MINOR_NR;

	printk(KERN_INFO "Check if the ring buffer is already init");
	if(kfifo_esize(ringbuf_dev.fifo_addr) != RINGBUF_SZ) {
		
		printk(KERN_INFO "Start to init the ring buffer\n");
		kfifo_alloc(ringbuf_dev, RINGBUF_SZ, GFP_KERNEL);

	}

	printk(KERN_INFO "Check if the payloads area is already init");
	//TODO: init the payloads linklist
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

	printk(KERN_INFO "release ringbuf_device\n");

   	return 0;
}



static int ringbuf_probe_device (struct pci_dev *pdev,
					const struct pci_device_id * ent) {

	int ret;
    struct ringbuf_device *dev = &ringbuf_dev;
    printk(KERN_INFO "probing for device\n");

    ret = pci_enable_device(pdev);
    if (ret < 0) {
        printk(KERN_INFO "unable to enable device: %d\n", ret);
        goto out;
    }

    ret = pci_request_regions(pdev, "ringbuf");
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

    printk(KERN_INFO "BAR0: 0x%0x, %d\n", dev->bar0_addr,
            dev->bar0_size);
    printk(KERN_INFO "BAR1: 0x%0x, %d\n", dev->bar1_addr,
            dev->bar1_size);
    printk(KERN_INFO "BAR2: 0x%0x, %d\n", dev->bar2_addr,
            dev->bar2_size);

    dev->regs_addr = ioremap(dev->bar0_addr, dev->bar0_len);
    if (!dev->regs_addr) {
        printk(KERN_INFO "unable to ioremap bar0, sz: %d\n", dev->bar0_len);
        goto release_regions;
    }

    dev->base_addr = ioremap(dev->bar2_addr, dev->bar2_len);
    if (!dev->base_addr) {
        printk(KERN_INFO "unable to ioremap bar2, sz: %d\n", dev->bar2_len);
        goto iounmap_bar0;
    }
    printk(KERN_INFO "BAR2 map: %p\n", dev->base_addr);

	ringbuf_dev.fifo_addr = (kfifo*)ringbuf_dev.base_addr;
	ringbuf_dev.payloads_st = ringbuf_dev.base_addr + sizeof(kfifo);	

    dev->dev = pdev;

    if (dev->revision == 1) {
        /* Only process the MSI-X interrupt. */
        dev->ivposition = ioread32(dev->regs_addr + IVPOSITION_REG_OFF);

        printk(KERN_INFO "device ivposition: %u, MSI-X: %s\n", dev->ivposition,
                (dev->ivposition == 0) ? "no": "yes");

        if (dev->ivposition != 0) {
            ret = request_msix_vectors(dev, 4);
            if (ret != 0) {
                goto destroy_device;
            }
        }
    }

    printk(KERN_INFO "device probed\n");
    return 0;

destroy_device:
    dev->dev = NULL;

iounmap_bar2:
    iounmap(dev->base_addr);

iounmap_bar0:
    iounmap(dev->regs_addr);

release_regions:
    pci_release_regions(pdev);

disable_device:
    pci_disable_device(pdev);

out:
    return ret;
}



static void ringbuf_remove_device(struct pci_dev* pdev)
{
    struct ringbuf_devide *dev = &ringbuf_dev;

    printk(KERN_INFO "removing ivshmem device\n");

    free_msix_vectors(dev);

    dev->dev = NULL;

    iounmap(dev->base_addr);
    iounmap(dev->regs_addr);

    pci_release_regions(pdev);
    pci_disable_device(pdev);
}



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