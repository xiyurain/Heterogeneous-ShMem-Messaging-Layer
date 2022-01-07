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
#include <linux/slab.h>

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
#define MAX_MSG_TYPE 16
#define MIN_MSG_CTRLTYPE 8
#define MAX_NAMESPACE_NUM 8
#define MAX_SOCKET_NUM 64
#define QEMU_PROCESS_ID 1
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define SLEEP_PERIOD_MSEC 10
#define DEVNAME "ringbuf"

#define IOCTL_MAGIC		('f')
#define IOCTL_REG_NAMESPACE	_IOW(IOCTL_MAGIC, 1, u32)
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

enum {
	/* message type*/
	msg_type_req 		=	1,
	msg_type_add		=	2,
	msg_type_free		=	3,
	/* control message types*/
	msg_type_conn 		=	8,
	msg_type_accept		=	9,
	msg_type_disconn	=	10,
	msg_type_kalive 	= 	11,
	msg_type_ack		=	12,
};

/*
 * message sent via ring buffer, as header of the payloads
*/
typedef struct ringbuf_msg_hd {
	unsigned int src_node;
	unsigned int msg_type;
	unsigned int is_sync;
	unsigned int payload_off;
	ssize_t payload_len;
} rbmsg_hd;

typedef STRUCT_KFIFO(char, RINGBUF_SZ) fifo;

/*
 * @ioaddr: physical address of IVshmem IO space
 * @fifo_host_addr: address of the Kfifo struct
 * @payload_area: start address of the payloads area
 */
typedef struct pcie_buffer {
	fifo			fifo_host2guest;
	fifo			fifo_guest2host;

	unsigned int 		notify_guest;
	unsigned int 		notify_host;
};

typedef struct pcie_port {
	void __iomem		*buffer_addr;
	fifo 			*fifo_send;
	fifo			*fifo_recv;

	unsigned int		*notify_remote;
	unsigned int 		*be_notified;
	unsigned int		notified_history;
} pcie_port;

typedef struct ringbuf_socket {
	char 			name[64];
	int 			in_use;
	int 			listening;
	int 			namespace_index;
	void			*belongs_endpoint;		

	pcie_port		*port_pcie;

	int			sync_toggle;
	struct timer_list 	keep_alive_timer;
} ringbuf_socket;

/*
 * ringbuf_device: Information about the PCIE device
 *
 * @base_addr: mapped start address of IVshmem space
 * @regaddr: physical address of shmem PCIe dev regs
*/
typedef struct ringbuf_device {
	struct pci_dev		*dev;
	u8 			revision;
	unsigned int 		ivposition;

	void __iomem 		*regs_addr;
	void __iomem 		*base_addr;
	unsigned int 		bar0_addr;
	unsigned int 		bar0_size;
	unsigned int 		bar1_addr;
	unsigned int 		bar1_size;
	unsigned int 		bar2_addr;
	unsigned int 		bar2_size;

} ringbuf_device;

typedef int (*msg_handler)(ringbuf_socket *socket, rbmsg_hd *hd);

typedef struct namespace {
	char 			namespace_name[64];
	msg_handler 		msg_handlers[MSG_TYPE_MAX];
} namespace;

typedef struct ringbuf_endpoint {
	ringbuf_device 		*device;
	unsigned int 		remote_node_id;
	unsigned int 		role;

	void __iomem		*mem_pool_area;
	struct gen_pool		*mem_pool;

	ringbuf_socket		syswide_socket;
	struct workqueue_struct *syswide_queue;
	struct work_struct	syswide_work;

	ringbuf_socket		sockets[MAX_SOCKET_NUM];
	namespace 		namespaces[NAMESPACE_NUM_MAX];
} ringbuf_endpoint;

static ringbuf_endpoint ringbuf_endpoints[MAX_ENDPOINT_NUM];


/*API directly on the PCIE port*/
static int pcie_poll(pcie_port *port);
static void pcie_notify(void *addr);
static int pcie_recv_msg(pcie_port *port, rbmsg_hd *hd);
static int pcie_send_msg(pcie_port *port, rbmsg_hd *hd);

/*API of the ringbuf socket*/
static void socket_bind(
	ringbuf_socket *socket, unsigned long buffer_addr, int role);
static void socket_listen(ringbuf_endpoint *ep, ringbuf_socket *socket);
static void socket_connect(ringbuf_endpoint *ep, ringbuf_socket *socket);
static void socket_send_sync(ringbuf_socket *socket, rbmsg_hd *hd);
static void socket_send_async(ringbuf_socket *socket, rbmsg_hd *hd);
static void socket_receive(ringbuf_socket *socket, rbmsg_hd *hd);
static void socket_disconnect(ringbuf_socket *socket);
static int socket_keepalive(ringbuf_socket *socket);

/*API of the ringbuf endpoint*/
static ringbuf_socket* endpoint_create_socket(
	ringbuf_device *dev, int namespace_index, char *socket_name);
static void endpoint_register_msg_handler(
	port_namespace *namespace, int msg_type, msg_handler handler);
static void endpoint_unregister_msg_handler(
	port_namespace *namespace, int msg_type);
static void endpoint_syswide_poll(struct work_struct *work);
static unsigned long endpoint_add_payload(ringbuf_port *port, size_t len);
static void endpoint_free_payload(ringbuf_port *port, rbmsg_hd *hd);

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

/*message handlers*/
static int handle_sys_conn(ringbuf_socket *socket, rbmsg_hd *hd);
static int handle_sys_accept(ringbuf_socket *socket, rbmsg_hd *hd);
static int handle_sys_kalive(ringbuf_socket *socket, rbmsg_hd *hd);
static int handle_sys_disconn(ringbuf_socket *socket, rbmsg_hd *hd);
static int handle_sys_ack(ringbuf_socket *socket, rbmsg_hd *hd);
static int handle_sys_req(ringbuf_socket *socket, rbmsg_hd *hd);
static int handle_sys_add(ringbuf_socket *socket, rbmsg_hd *hd);
static int handle_sys_free(ringbuf_socket *socket, rbmsg_hd *hd);

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


/* ================================================================================================
 * Definition of the functions
 */
static int pcie_poll(pcie_port *port) 
{
	if(*(port->be_notified) > port->notified_history) {
		port->notify_host_history++;
		return 1;
	}
	return 0;
}

static void pcie_notify(void *addr) 
{
	(*(unsigned int*)addr)++;
}

static int pcie_recv_msg(pcie_port *port, rbmsg_hd *hd)
{
	size_t ret_len;
	fifo *fifo_addr = port->fifo_recv;

	if(kfifo_len(fifo_addr) < MSG_SZ) {
		printk(KERN_ERR "no msg in ring buffer\n");
		return -1;
	}

	fifo_addr->kfifo.data = (void*)fifo_addr + 0x18;
	mb();
	ret_len = kfifo_out(fifo_addr, (char*)hd, MSG_SZ);
		
	if(!hd->src_node || !ret_len) {
		printk(KERN_ERR "invalid ring buffer msg\n");
		return -1;
	}
	return 0;
}

static int pcie_send_msg(pcie_port *port, rbmsg_hd *hd)
{
	fifo *fifo_addr = port->fifo_send;

	if(kfifo_avail(fifo_addr) < MSG_SZ) {
		printk(KERN_ERR "not enough space in ring buffer\n");
		return -1;
	}

	fifo_addr->kfifo.data = (void*)fifo_addr + 0x18;
	rmb();
	kfifo_in(fifo_addr, (char*)hd, MSG_SZ);
	
	pcie_notify(port->notify_remote);
	return 0;
}

static void socket_bind(ringbuf_socket *socket, unsigned long buffer_addr, 
								int role) 
{
	fifo fifo_st;
	struct pcie_buffer *buffer = (struct pcie_buffer*)buffer_addr;
	pcie_port *port = kmalloc(sizeof(pcie_port), GFP_KERNEL);
	port->buffer_addr = buffer;

	if(role == Host) {
		port->fifo_send = &buffer->fifo_host2guest;
		port->fifo_recv = &buffer->fifo_guest2host;
		memcpy(&buffer->fifo_host2guest, &fifo_st, sizeof(fifo_st));
		memcpy(&buffer->fifo_guest2host, &fifo_st, sizeof(fifo_st));
		INIT_KFIFO(buffer->fifo_host2guest);
		INIT_KFIFO(buffer->fifo_guest2host);
		
		port->notify_remote = &buffer->notify_guest;
		port->be_notified = &buffer->notify_host;
		*port->notified_remote = 0;
		*port->be_notified = 0;
		port->notified_history = 0;
	} else {
		port->fifo_send = &buffer->fifo_guest2host;
		port->fifo_recv = &buffer->fifo_host2guest;
		port->notify_remote = &buffer->notify_host;
		port->be_notified = &buffer->notify_guest;
		port->notified_history = 0;
	}

	socket->port_pcie = port;
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

static int handle_sys_conn(ringbuf_socket *socket, rbmsg_hd *hd) {
	int i;
	ringbuf_endpoint *ep = (ringbuf_endpoint*)socket->belongs_endpoint;
	
	if(ep->role != Host) {
		return -1;
	}
	for(i = 0; i < MAX_SOCKET_NUM; ++i)
		if(ep->sockets[i].in_use && ep->sockets[i].listening)
			break;

	socket_accept(ep->sockets + i, hd);
	socket_send_async(ep->sockets + i, hd);
	
	return 0;
}

static int handle_sys_accept(ringbuf_socket *socket, rbmsg_hd *hd) {
	int i;
	ringbuf_endpoint *ep = (ringbuf_endpoint*)socket->belongs_endpoint;
	ringbuf_socket *new_socket;

	if(ep->role != Guest) {
		return -1;
	}
	for(i = 0; i < MAX_SOCKET_NUM; ++i) {
		new_socket = ep->sockets + i;
		if(new_socket->in_use && new_socket->listening 
			&& new_socket->namespace_index == hd->payload_len)
			break;
	}

	new_socket->listening = FALSE;
	socket_bind(new_socket, ep->mem_pool_area + hd->payload_off, ep->role);

	return 0;
}

static int handle_sys_kalive(ringbuf_socket *socket, rbmsg_hd *hd) {
	socket->sync_toggle = TRUE;
}

static int handle_sys_disconn(ringbuf_socket *socket, rbmsg_hd *hd) {
	//TODO
}

static int handle_sys_ack(ringbuf_socket *socket, rbmsg_hd *hd) 
{
	return 0;
}
		
static int handle_msg_type_req(ringbuf_socket *socket, rbmsg_hd *hd) 
{
	rbmsg_hd new_hd;
	char buffer[256];
	size_t pld_len;

	sprintf(buffer, "msg dst_id=%d src_id=%d - (jiffies: %lu)",
			hd->src_qid, dev->ivposition, jiffies);
	pld_len = strlen(buffer) + 1;

	new_hd.src_qid = NODEID;
	new_hd.msg_type = msg_type_add;
	new_hd.payload_off = endpoint_add_payload(
			(ringbuf_endpoint*)socket->belongs_endpoint, len);
	new_hd.payload_len = pld_len;
	new_hd.is_sync = 0;

	memcpy(((ringbuf_endpoint*)socket->belongs_endpoint)->mem_pool_area
					+ new_hd.payload_off, buffer, len);
	wmb();
	socket_send_async(socket, &new_hd);

	return 0;
}

static int handle_msg_type_add(ringbuf_socket *socket, rbmsg_hd *hd)
{
	char buffer[256];

	memcpy(	buffer, 
		((ringbuf_endpoint*)socket->belongs_endpoint)->mem_pool_area
			+ hd->payload_off, 
		hd->payload_len);
	rmb();
	
	printk(KERN_INFO "PAYLOAD_CONTENT: %s\n", buffer);

	hd->msg_type = msg_type_free;
	socket_send_async(socket, hd);
	return 0;
}

static int handle_msg_type_free(ringbuf_socket *socket, rbmsg_hd *hd)
{
	endpoint_free_payload((ringbuf_endpoint*)socket->belongs_endpoint, hd);
	return 0;
}

static ringbuf_socket* endpoint_create_socket(ringbuf_endpoint *ep, 
					int nsp_index, char *socket_name) {
	int i;
	ringbuf_socket *socket;
	unsigned long buffer_offset;

	for(i = 0; i < MAX_SOCKET_NUM; i++) {
		if(ep->sockets[i].in_use == FALSE)
			break;
	}
	socket = ep->sockets + i;

	socket->in_use = TRUE;
	socket->belongs_endpoint = ep;
	strcpy(socket->name, socket_name);
	socket->namespace_index = nsp_index;
	socket->listening = FALSE;
	
	socket->sync_toggle = 0;
	if(ep->role == Host) {
		buffer_offset = endpoint_add_payload(ep, sizeof(struct pcie_buffer));
		socket_bind(socket, ep->device->base_addr + buffer_offset, ep->role);
	}
	
	return socket;
}

static void endpoint_syswide_poll(struct work_struct *work) 
{
	rbmsg_hd hd;
	ringbuf_socket *socket = (ringbuf_socket*)work->data;
	while (TRUE) {
		socket_receive(socket, &hd);
		msleep(SLEEP_PERIOD_MSEC);
	}
}

static void endpoint_register_msg_handler(ringbuf_endpoint *ep,
			int nsp_index, int msg_type, msg_handler handler) {
	if(handler == NULL || msg_type > MAX_MSG_TYPE || msg_type <= 0) {
		return;
	}
	if(nsp_index > MAX_NAMESPACE_NUM || nsp_index < 0){
		return;
	}
	ep->namespaces[nsp_index].msg_handlers[msg_type] = handler;
}

static void endpoint_unregister_msg_handler(ringbuf_endpoint *ep, 
					int nsp_index, int msg_type) {
	if(handler == NULL || msg_type > MAX_MSG_TYPE || msg_type <= 0) {
		return;
	}
	if(nsp_index > MAX_NAMESPACE_NUM || nsp_index < 0){
		return;
	}
	ep->namespaces[nsp_index].msg_handlers[msg_type] = NULL;
}

static unsigned long endpoint_add_payload(ringbuf_endpoint *ep, size_t len) {
	unsigned long payload_addr;
	unsigned long offset;

	payload_addr = gen_pool_alloc(ep->mem_pool, len);
	offset = payload_addr - ep->mem_pool_area;

	// printk(KERN_INFO "alloc payload memory at offset: %lu\n", offset);
	return offset;
}

static void endpoint_free_payload(ringbuf_endpoint *ep, rbmsg_hd *hd) {
	gen_pool_free(ep->mem_pool, 
		ep->mem_pool_area + hd->payload_off, hd->payload_len);
	// printk(KERN_INFO "free payload memory at offset: %lu\n", node->offset);
}

/*================================================================================================*/

static long ringbuf_ioctl(struct file *fp, unsigned int cmd,  long unsigned int value)
{
    	switch (cmd) {
    	//case IOCTL_REG_NAMESPACE:
	// case IOCTL_REQ:
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
