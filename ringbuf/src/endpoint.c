#include "endpoint.h"

static void endpoint_init_dev(ringbuf_device *dev, struct pci_dev *pdev) {
	dev = kmalloc(sizeof(ringbuf_device), GFP_KERNEL);

	pci_read_config_byte(pdev, PCI_REVISION_ID, &(dev->revision));
	printk(KERN_INFO "device %d:%d, revision: %d\n", 
				device_major_nr, 0, dev->revision);

	dev->bar0_addr = pci_resource_start(pdev, 0);
	dev->bar0_size = pci_resource_len(pdev, 0);
	dev->bar1_addr = pci_resource_start(pdev, 1);
	dev->bar1_size = pci_resource_len(pdev, 1);
	dev->bar2_addr = pci_resource_start(pdev, 2);
	dev->bar2_size = pci_resource_len(pdev, 2);

	dev->dev = pdev;
	dev->ivposition = NODEID;
	pci_set_drvdata(pdev, ep);
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
}

static void endpoint_init(struct pci_dev *pdev, unsigned int role) {
	ringbuf_endpoint *ep;

	for(i = 0; i < MAX_ENDPOINT_NUM; ++i) 
		if(ringbuf_endpoints[i].device == NULL)
			break;
	ep = ringbuf_endpoints + i;
	endpoint_init_dev(ep->dev, pdev);
	
	//TODO: get ep->remote_node_id

	if(((int)pci_name(pdev)[9] - 48) % 2)
		ep->role = Host;
	else
		ep->role = Guest;

	ep->mem_pool_area = ep->device->base_addr + sizeof(pcie_buffer) + 64;	
	if(ep->role == Host) {
		ep->mem_pool = gen_pool_create(0, -1);
		if(gen_pool_add(ep->mem_pool, ep->mem_pool_area, 3 << 20, -1)) {
			printk(KERN_INFO "gen_pool create failed!!!!!");
			return;
		}
	}

	// socket_bind(&ep->syswide_socket, ep->device->base_addr, ep->role);
	// ep->syswide_socket.namespace_index = sys;
	// ep->syswide_socket.belongs_endpoint = ep;
	// ep->syswide_queue = alloc_workqueue("syswide", 0, 0);
	// INIT_WORK(&ep->syswide_work, endpoint_syswide_poll);
	// ep->syswide_work->data = (unsigned long)(&ep->syswide_socket);
	// queue_work(ep->syswide_queue, &ep->syswide_work);
}

static void endpoint_destroy(unsigned int remote_id, unsigned int role) {

}

static pcie_port *endpoint_alloc_port(ringbuf_endpoint *ep) {

}

static int endpoint_free_port(ringbuf_endpoint *ep) {

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

static void endpoint_syswide_poll(struct work_struct *work) 
{
	rbmsg_hd hd;
	ringbuf_socket *socket = (ringbuf_socket*)work->data;
	while (TRUE) {
		socket_receive(socket, &hd);
		msleep(SLEEP_PERIOD_MSEC);
	}
}

// static void endpoint_register_msg_handler(ringbuf_endpoint *ep,
// 			int nsp_index, int msg_type, msg_handler handler) {
// 	if(handler == NULL || msg_type > MAX_MSG_TYPE || msg_type <= 0) {
// 		return;
// 	}
// 	if(nsp_index > MAX_NAMESPACE_NUM || nsp_index < 0){
// 		return;
// 	}
// 	ep->namespaces[nsp_index].msg_handlers[msg_type] = handler;
// }

// static void endpoint_unregister_msg_handler(ringbuf_endpoint *ep, 
// 					int nsp_index, int msg_type) {
// 	if(handler == NULL || msg_type > MAX_MSG_TYPE || msg_type <= 0) {
// 		return;
// 	}
// 	if(nsp_index > MAX_NAMESPACE_NUM || nsp_index < 0){
// 		return;
// 	}
// 	ep->namespaces[nsp_index].msg_handlers[msg_type] = NULL;
// }

// static ringbuf_socket* endpoint_create_socket(ringbuf_endpoint *ep, 
// 					int nsp_index, char *socket_name) {
// 	int i;
// 	ringbuf_socket *socket;
// 	unsigned long buffer_offset;

// 	for(i = 0; i < MAX_SOCKET_NUM; i++) {
// 		if(ep->sockets[i].in_use == FALSE)
// 			break;
// 	}
// 	socket = ep->sockets + i;

// 	socket->in_use = TRUE;
// 	socket->belongs_endpoint = ep;
// 	strcpy(socket->name, socket_name);
// 	socket->namespace_index = nsp_index;
// 	socket->listening = FALSE;
	
// 	socket->sync_toggle = 0;
// 	if(ep->role == Host) {
// 		buffer_offset = endpoint_add_payload(ep, sizeof(struct pcie_buffer));
// 		socket_bind(socket, ep->device->base_addr + buffer_offset, ep->role);
// 	}
	
// 	return socket;
// }
