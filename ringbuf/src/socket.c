#include "socket.h"

static ringbuf_socket *socket_create(char *name, char *service) 
{
	ringbuf_socket *sock = kmalloc(sizeof(ringbuf_socket), GFP_KERNEL);
	memset(sock, 0, sizeof(ringbuf_socket));

	strcpy(sock->name, name);
	strcpy(sock->service, service);

	return sock;	
}

static void socket_bind(ringbuf_socket *sock, 
		unsigned int remote_id, int role, unsigned long buf_addr) 
{
	int i;

	for(i = 0; i < MAX_ENDPOINT_NUM; ++i) 
		if(ringbuf_endpoints[i].remote_node_id == remote_id &&
					ringbuf_endpoints[i].role == role)
			break;

	sock->bind_endpoint = ringbuf_endpoints + i;
	sock->bind_port = endpoint_alloc_port(sock->bind_endpoint, buf_addr);
}

static void socket_listen(ringbuf_socket *sock)
{
	sock->is_listening = TRUE;
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

static void socket_accept(ringbuf_socket *socket, rbmsg_hd *hd) 
{
	hd->is_sync = TRUE;
	hd->msg_type = msg_type_accept;
	hd->payload_off = socket->port_pcie.buffer_addr - ep->device.base_addr;
	hd->src_node = NODEID;

	socket->listening = FALSE;
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
