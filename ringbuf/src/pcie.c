#include "pcie.h"

/*
 * pcie_port_init: init a pcie_port and bind pcie ring buffer
 * port: the port to init
 * addr: the address of pcie ring buffer
 * role: role of the port
 */
static void pcie_port_init(pcie_port *port, unsigned long addr, int role)
{
	fifo fifo_st;
	struct pcie_buffer *buffer = (struct pcie_buffer*)addr;

	port = kmalloc(sizeof(pcie_port), GFP_KERNEL);
	//TODO: memset
	port->buffer_addr = buffer;
	port->notified_history = 0;

	if(role == Host) {
		port->fifo_send = &buffer->fifo_host2guest;
		port->fifo_recv = &buffer->fifo_guest2host;
		port->notify_remote = &buffer->notify_guest;
		port->be_notified = &buffer->notify_host;
		
		memcpy(&buffer->fifo_host2guest, &fifo_st, sizeof(fifo_st));
		memcpy(&buffer->fifo_guest2host, &fifo_st, sizeof(fifo_st));
		INIT_KFIFO(buffer->fifo_host2guest);
		INIT_KFIFO(buffer->fifo_guest2host);
		*port->notified_remote = 0;
		*port->be_notified = 0;
		
	} else {
		port->fifo_send = &buffer->fifo_guest2host;
		port->fifo_recv = &buffer->fifo_host2guest;
		port->notify_remote = &buffer->notify_host;
		port->be_notified = &buffer->notify_guest;
	}
}

/*
 * pcie_port_free: init a pcie_port and bind pcie ring buffer
 * port: the port to free
 */
static void pcie_port_free(pcie_port *port) {
	kfree(port);
}

/* 
 * pcie_poll: polling for a new notification in pcie_port
 * port: the port for polling
 */
static int pcie_poll(pcie_port *port) 
{
	if(*(port->be_notified) > port->notified_history) {
		port->notify_host_history++;
		return 1;
	}
	return 0;
}

/*
 * pcie_recv_msg: receive message from pcie port
 * port: port where new message arrives
 * hd: pointer to the header to store new message
 */
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

/*
 * pcie_send_msg: send message via pcie port
 * port: port where message be sent
 * hd: pointer to the header to be sent
 */
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
	
	(*port->notify_remote)++;
	return 0;
}
