#include "sys.h"

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
