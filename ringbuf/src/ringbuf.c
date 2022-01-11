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
#define MAX_ENDPOINT_NUM 8
#define MAX_PORT_NUM 16
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
#define IOCTL_IVPOSITION	_IOR(IOCTL_MAGIC, 3, u32)
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

/*namespace*/
enum {
	sys 	= 	0,
	net 	= 	1,
	fs	=	2,
	vm 	= 	3,
	proc 	=	4,
};

typedef int (*msg_handler)(ringbuf_socket *socket, rbmsg_hd *hd);

typedef struct ringbuf_socket {
	char 			name[64];
	int 			in_use;
	int 			listening;
	int 			service_index;

	pcie_port		*listening_port;

	int			sync_toggle;
	struct timer_list 	keep_alive_timer;
} ringbuf_socket;

static ringbuf_endpoint ringbuf_endpoints[MAX_ENDPOINT_NUM];

typedef struct service {
	char 			name[64];
	msg_handler 		msg_handlers[MAX_MSG_TYPE];
	ringbuf_socket		*sockets[MAX_SOCKET_NUM];
} service;

static service services[MAX_SERVICE_NUM];

/*API of the ringbuf socket*/
static void socket_bind(ringbuf_socket *socket, unsigned long addr, int role);
static void socket_listen(ringbuf_endpoint *ep, ringbuf_socket *socket);
static void socket_connect(ringbuf_endpoint *ep, ringbuf_socket *socket);
static void socket_send_sync(ringbuf_socket *socket, rbmsg_hd *hd);
static void socket_send_async(ringbuf_socket *socket, rbmsg_hd *hd);
static void socket_receive(ringbuf_socket *socket, rbmsg_hd *hd);
static void socket_disconnect(ringbuf_socket *socket);
static int socket_keepalive(ringbuf_socket *socket);

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

static void socket_bind(ringbuf_socket *socket, unsigned int remote_id) 
{
	int i;
	for(i = 0; i < MAX_ENDPOINT_NUM; i++) 
		if(ringbuf_endpoints[i].remote_node_id == remote_id 
				&& ringbuf_endpoints[i].role == Guest)
			break;
	

}

static void socket_listen(ringbuf_socket *socket) 
{
	socket->listening = TRUE;
}

static void socket_accept(ringbuf_socket *socket, rbmsg_hd *hd) 
{
	hd->is_sync = TRUE;
	hd->msg_type = msg_type_accept;
	hd->payload_off = socket->port_pcie.buffer_addr - ep->device.base_addr;
	hd->src_node = NODEID;

	socket->listening = FALSE;
}

static void socket_connect(ringbuf_socket *socket) 
{
	rbmsg_hd hd;
	
	hd.msg_type = msg_type_conn;
	hd.src_qid = dev->ivposition;
	hd.is_sync = FALSE;
	hd.payload_len = socket->namespace_index;

	socket->listening = TRUE;
	socket_send_async(sys_socket->port_pcie, &hd);	
}

static void socket_send_sync(ringbuf_socket *socket, rbmsg_hd *hd) 
{
	hd->is_sync = TRUE;
	pcie_send_msg(socket->port_pcie, hd);
	
	while (!pcie_poll(socket->port_pcie));
	pcie_recv_msg(socket->port_pcie, hd);
	if(hd->msg_type != msg_type_ack) {
		printk(KERN_INFO "socket_send_sync went wrong!!!\n");
	}
}

static void socket_send_async(ringbuf_socket *socket, rbmsg_hd *hd) 
{
	pcie_send_msg(socket->port_pcie, hd);
}

static void socket_receive(ringbuf_socket *socket, rbmsg_hd *hd) 
{	
	rbmsg_hd hd_ack;
	namespace *namespc;
	msg_handler handler = NULL;

	while(!pcie_poll(socket->port_pcie));
	pcie_recv_msg(socket->port_pcie, &hd);

	if(hd->is_sync == TRUE){
		hd_ack.is_sync = 0;
		hd_ack.msg_type = msg_type_ack;
		hd_ack.payload_off = hd->payload_off;
		hd_ack.src_node = hd->src_node;
		pcie_send_msg(socket->port_pcie, &hd_ack);
	}

	ringbuf_endpoint *ep = (ringbuf_endpoint*)socket->belongs_endpoint;
	namespc = ep->namespaces + socket->namespace_index;
	handler = namespc->msg_handlers[hd->msg_type];
	handler(socket, hd);
}

static int socket_keepalive(ringbuf_socket *socket) 
{
	rbmsg_hd hd;
	hd.msg_type = msg_type_kalive;
	hd.src_qid = NODEID;
	hd.is_sync = FALSE;

	socket_send_sync(sockcet, &hd);
	msleep(10000);
	if(socket->sync_toggle) {
		socket->sync_toggle = 0;
		return 0;
	} else {
		return -1;
	}
}

static void socket_disconnect(ringbuf_socket *socket) 
{
	//TODO
}

static long ringbuf_ioctl(struct file *fp, unsigned int cmd,  long unsigned int value)
{
	int remote_id;
	ringbuf_endpoint *ep;

    	switch (cmd) {
    	case IOCTL_SOCKET://TODO
		// remote_id = *((int*)value);

		// *(void*)value = ep-> 

	case IOCTL_IVPOSITION:
		printk(KERN_INFO "get ivposition: %d\n", NODEID);
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
	int i;
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

	for(i = 0; i < MAX_ENDPOINT_NUM; ++i) 
		if(ringbuf_endpoints[i].device == NULL)
			break;
	ep = ringbuf_endpoints + i;
	ep->device = kmalloc(sizeof(ringbuf_device), GFP_KERNEL);

	pci_read_config_byte(pdev, PCI_REVISION_ID, &(ep->device->revision));
	printk(KERN_INFO "device %d:%d, revision: %d\n", 
			device_major_nr, 0, ep->device->revision);

	ep->device->bar0_addr = pci_resource_start(pdev, 0);
	ep->device->bar0_size = pci_resource_len(pdev, 0);
	ep->device->bar1_addr = pci_resource_start(pdev, 1);
	ep->device->bar1_size = pci_resource_len(pdev, 1);
	ep->device->bar2_addr = pci_resource_start(pdev, 2);
	ep->device->bar2_size = pci_resource_len(pdev, 2);

	ep->device->dev = pdev;
	ep->device->ivposition = NODEID;
	pci_set_drvdata(pdev, ep);
	ep->device->regs_addr = ioremap(
			ep->device->bar0_addr, ep->device->bar0_size);
	if (!ep->device->regs_addr) {
		printk(KERN_INFO "unable to ioremap bar0, sz: %d\n", 
						ep->device->bar0_size);
		goto release_regions;
	}
	ep->device->base_addr = ioremap(
			ep->device->bar2_addr, ep->device->bar2_size);
	if (!ep->device->base_addr) {
		printk(KERN_INFO "unable to ioremap bar2, sz: %d\n", 
						ep->device->bar2_size);
		goto iounmap_bar0;
	}
	//TODO: get ep->remote_node_id
	if(((int)pci_name(pdev)[9] - 48) % 2){
		ep->role = Host;
		printk(KERN_INFO "I'm a host!!!!!!!!!!!!");
	} else {
		ep->role = Guest;
		printk(KERN_INFO "I'm a Guest!!!!!!!!!!!!");
	}

	ep->mem_pool_area = ep->device->base_addr + sizeof(pcie_buffer) + 64;	
	if(ep->role == Host) {
		ep->mem_pool = gen_pool_create(0, -1);
		if(gen_pool_add(ep->mem_pool, ep->mem_pool_area, 3 << 20, -1)) {
			printk(KERN_INFO "gen_pool create failed!!!!!");
			return;
		}
	}

	socket_bind(&ep->syswide_socket, ep->device->base_addr, ep->role);
	ep->syswide_socket.namespace_index = sys;
	ep->syswide_socket.belongs_endpoint = ep;
	ep->syswide_queue = alloc_workqueue("syswide", 0, 0);
	INIT_WORK(&ep->syswide_work, endpoint_syswide_poll);
	ep->syswide_work->data = (unsigned long)(&ep->syswide_socket);
	queue_work(ep->syswide_queue, &ep->syswide_work);

	endpoint_register_msg_handler(ep, sys, msg_type_conn, handle_sys_conn);
	endpoint_register_msg_handler(ep, sys, msg_type_accept, handle_sys_accept);
	endpoint_register_msg_handler(ep, sys, msg_type_disconn, handle_sys_disconn);
	endpoint_register_msg_handler(ep, sys, msg_type_kalive, handle_sys_kalive);
	endpoint_register_msg_handler(ep, sys, msg_type_ack, handle_sys_ack);
	endpoint_register_msg_handler(ep, sys, msg_type_req, handle_sys_req);
	endpoint_register_msg_handler(ep, sys, msg_type_add, handle_sys_add);
	endpoint_register_msg_handler(ep, sys, msg_type_free, handle_sys_free);

	printk(KERN_INFO "device probed\n");
	return 0;

iounmap_bar0:
    	iounmap(ep->device->regs_addr);

release_regions:
    	ep->device->dev = NULL;
    	pci_release_regions(pdev);

disable_device:
    	pci_disable_device(pdev);

out:
    	return ret;
}

static void ringbuf_remove_device(struct pci_dev* pdev)
{
	ringbuf_endpoint *ep = pci_get_drvdata(pdev);

	printk(KERN_INFO "removing ivshmem device\n");
	
	//TODO: free the endpoint!
	ep->device->dev = NULL;
	iounmap(ep->device->regs_addr);
	iounmap(ep->device->base_addr);

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
