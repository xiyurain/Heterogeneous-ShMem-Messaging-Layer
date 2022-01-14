#include "endpoint.h"

static void endpoint_init_dev(ringbuf_device *dev, struct pci_dev *pdev) {
	dev = kmalloc(sizeof(ringbuf_device), GFP_KERNEL);
	memset(dev, 0, sizeof(ringbuf_device));

	pci_read_config_byte(pdev, PCI_REVISION_ID, &(dev->revision));
	printk(KERN_INFO "device %d:%d, revision: %d\n", 
				device_major_nr, 0, dev->revision);

	dev->bar0_addr = pci_resource_start(pdev, 0);
	dev->bar0_size = pci_resource_len(pdev, 0);
	dev->bar1_addr = pci_resource_start(pdev, 1);
	dev->bar1_size = pci_resource_len(pdev, 1);
	dev->bar2_addr = pci_resource_start(pdev, 2);
	dev->bar2_size = pci_resource_len(pdev, 2);

	dev->pcie_dev = pdev;
	dev->ivposition = NODEID;
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

static void endpoint_destroy_dev(ringbuf_device *dev) {
	iounmap(dev->regs_addr);
	iounmap(dev->base_addr);

	kfree(dev);
	dev = NULL;
}

static ringbuf_endpoint *endpoint_init(struct pci_dev *pdev) {
	int i;
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
		if(gen_pool_add(ep->mem_pool, 
					ep->mem_pool_area, 3 << 20, -1)) {
			printk(KERN_INFO "gen_pool create failed!!!!!");
			return;
		}
	}

	pci_set_drvdata(pdev, ep);
	return ep;
}

static void endpoint_destroy(struct pci_dev *pdev) {
	ringbuf_endpoint *ep = pci_get_drvdata(pdev);
	pci_set_drvdata(pdev, NULL);

	gen_pool_destroy(ep->mem_pool);
	memset(ep->mem_pool_area, 0, 3<<20);

	endpoint_destroy_dev(ep->device);
}

static pcie_port *endpoint_alloc_port(ringbuf_endpoint *ep, 
					unsigned long buf_addr) {
	int i;
	pcie_port *port;

	for(i = 0; i < MAX_PORT_NUM; ++i) {
		if(ep->pcie_ports[i] == NULL)
			break;
	port = ep->pcie_ports[i];

	if(ep->role == Host)
		buf_addr = endpoint_add_payload(ep, sizeof(pcie_buffer));
	pcie_port_init(port, buf_addr, ep->role);

	return port;
}

static int endpoint_free_port(ringbuf_endpoint *ep, pcie_port *port) {
	int i;
	
	for(i = 0; i < MAX_PORT_NUM; ++i) 
		if(ep->pcie_ports[i] == port)
			break;
	if(i == MAX_PORT_NUM)
		return -1;
	
	endpoint_free_payload(ep, port->buffer_addr, sizeof(pcie_buffer));
	pcie_port_free(port);

	return 0;
}

static unsigned long endpoint_add_payload(ringbuf_endpoint *ep, size_t len) {
	unsigned long payload_addr;
	unsigned long offset;

	payload_addr = gen_pool_alloc(ep->mem_pool, len);
	offset = payload_addr - ep->mem_pool_area;

	// printk(KERN_INFO "alloc payload memory at offset: %lu\n", offset);
	return offset;
}

static void endpoint_free_payload(ringbuf_endpoint *ep, 
				unsigned long addr, size_t len) {
	gen_pool_free(ep->mem_pool, addr, len);
	// printk(KERN_INFO "free payload memory at offset: %lu\n", node->offset);
}
