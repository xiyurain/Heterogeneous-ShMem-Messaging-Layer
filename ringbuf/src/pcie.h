#define MSG_SZ sizeof(rbmsg_hd)

typedef struct ringbuf_msg_hd {
	unsigned int src_node;
	unsigned int msg_type;
	unsigned int is_sync;
	unsigned int payload_off;
	ssize_t payload_len;
	char service[16];
} rbmsg_hd;

typedef STRUCT_KFIFO(char, RINGBUF_SZ) fifo;

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


/*API directly on the PCIE port*/
static void pcie_port_init(pcie_port *port, unsigned long addr, int role);
static void pcie_port_free(pcie_port *port);
static int pcie_poll(pcie_port *port);
static int pcie_recv_msg(pcie_port *port, rbmsg_hd *hd);
static int pcie_send_msg(pcie_port *port, rbmsg_hd *hd);