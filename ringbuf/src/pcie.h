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

typedef struct ringbuf_msg_hd {
	unsigned int src_node;
	unsigned int msg_type;
	unsigned int is_sync;
	unsigned int payload_off;
	ssize_t payload_len;
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
static int pcie_poll(pcie_port *port);
static int pcie_recv_msg(pcie_port *port, rbmsg_hd *hd);
static int pcie_send_msg(pcie_port *port, rbmsg_hd *hd);