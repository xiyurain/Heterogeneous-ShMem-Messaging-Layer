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

static void socket_connect(ringbuf_socket *sock) 
{
	rbmsg_hd hd;
	ringbuf_endpoint *ep = sock->bind_endpoint;
	ringbuf_socket *sys_sock = ep->system_sock;

	hd.msg_type = msg_type_conn;
	hd.src_node = dev->ivposition;
	hd.is_sync = FALSE;
	strcpy(hd.service, sock->service);

	sock-> is_listening = TRUE;
	socket_send_async(sys_sock, &hd);	
}

static void socket_accept(ringbuf_socket *sock, rbmsg_hd *hd) 
{
	ringbuf_device *dev = sock->bind_endpoint->device;
	hd->src_node = dev->ivposition;

	hd->msg_type = msg_type_accept;
	hd->payload_off = sock->bind_port->buffer_addr - dev->base_addr;

	socket->listening = FALSE;
}

static void socket_send_sync(ringbuf_socket *sock, rbmsg_hd *hd) 
{
	hd->is_sync = TRUE;
	pcie_send_msg(sock->bind_port, hd);
	
	while(!pcie_poll(sock->bind_port));
	pcie_recv_msg(sock->bind_port, hd);
	if(hd->msg_type != msg_type_ack) {
		printk(KERN_INFO "socket_send_sync went wrong!!!\n");
	}
}

static void socket_send_async(ringbuf_socket *sock, rbmsg_hd *hd) 
{
	pcie_send_msg(sock->bind_port, hd);
}

static void socket_receive(ringbuf_socket *sock, rbmsg_hd *hd) 
{	
	rbmsg_hd hd_ack;
	// msg_handler handler = NULL;

	while(!pcie_poll(sock->bind_port));
	pcie_recv_msg(sock->bind_port, hd);

	if(hd->is_sync == TRUE){
		hd_ack.src_node = sock->bind_endpoint->device->ivposition;
		hd_ack.msg_type = msg_type_ack;
		hd_ack.is_sync = FALSE;
		hd_ack.payload_off = hd->payload_off;
		pcie_send_msg(sock->bind_port, &hd_ack);
	}
}

static int socket_keepalive(ringbuf_socket *sock) 
{
	rbmsg_hd hd;
	hd.src_node = sock->bind_endpoint->device->ivposition;
	hd.msg_type = msg_type_kalive;
	hd.is_sync = FALSE;

	socket_send_sync(sock, &hd);
	msleep(10000);
	if(sock->sync_toggle == TRUE) {
		sock->sync_toggle = FALSE;
		return 0;
	} else {
		return -1;
	}
}

static void socket_disconnect(ringbuf_socket *sock) 
{
	//TODO
}
