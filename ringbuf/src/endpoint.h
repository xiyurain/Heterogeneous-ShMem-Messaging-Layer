#include "pcie.h"
#define MAX_PORT_NUM 16
#define MAX_ENDPOINT_NUM 8

static ringbuf_endpoint ringbuf_endpoints[MAX_ENDPOINT_NUM];

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

	pcie_port		*syswide_port;
	pcie_port		*pcie_ports[MAX_PORT_NUM];
} ringbuf_endpoint;

/*API of the ringbuf endpoint*/
static void endpoint_init_dev(ringbuf_device *dev, struct pci_dev *pdev);
static void endpoint_init(struct pci_dev *pdev, unsigned int role);
static void endpoint_destroy(unsigned int remote_id, unsigned int role);
static pcie_port *endpoint_alloc_port(ringbuf_endpoint *ep);
static int endpoint_free_port(ringbuf_endpoint *ep);
static unsigned long endpoint_add_payload(ringbuf_endpoint *ep, size_t len);
static void endpoint_free_payload(ringbuf_endpoint *ep, rbmsg_hd *hd);
static void endpoint_syswide_poll(struct work_struct *work);