/*
 * ringbuf_device: Information about the PCIE device
 * @base_addr: mapped start address of IVshmem space
 * @regs_addr: physical address of shmem PCIe dev regs
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

typedef struct ringbuf_endpoint {
	ringbuf_device 		*device;
	unsigned int 		remote_node_id;
	unsigned int 		role;

	void __iomem		*mem_pool_area;
	struct gen_pool		*mem_pool;

	pcie_port		syswide_port;
	struct workqueue_struct *syswide_queue;
	struct work_struct	syswide_work;

	pcie_port		pcie_ports[MAX_PORT_NUM];
} ringbuf_endpoint;


/*API of the ringbuf endpoint*/
static ringbuf_socket* endpoint_alloc_port(
	ringbuf_device *dev, int namespace_index, char *socket_name);
static void endpoint_register_msg_handler(
	port_namespace *namespace, int msg_type, msg_handler handler);
static void endpoint_unregister_msg_handler(
	port_namespace *namespace, int msg_type);
static void endpoint_syswide_poll(struct work_struct *work);
static unsigned long endpoint_add_payload(ringbuf_port *port, size_t len);
static void endpoint_free_payload(ringbuf_port *port, rbmsg_hd *hd);