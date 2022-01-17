#include "sys_handler.h"

static void init_systemwide_service()
{
	int i;

	register_sys_handlers();

	for(i = 0; i < MAX_ENDPOINT_NUM; ++i)
		if(ringbuf_endpoints[i] != NULL)
			set_system_socket(ringbuf_endpoints[i]);
}

static void register_sys_handlers() 
{
	service *sys_service = kmalloc(sizeof(service), GFP_KERNEL);

	register_message(sys_service, msg_type_connect, sys_handle_connect);
	register_message(sys_service, msg_type_accept, sys_handle_accept);
	register_message(sys_service, msg_type_disconnect, sys_handle_disconnect);
	register_message(sys_service, msg_type_keepalive, sys_handle_keepalive);
	register_message(sys_service, msg_type_ack, sys_handle_ack);	

	register_service(sys_service);
}

static void set_system_socket(ringbuf_endpoint *ep)
{
	// namespc = ep->namespaces + socket->namespace_index;
	// handler = namespc->msg_handlers[hd->msg_type];
	// handler(socket, hd);
	
	// socket_bind(&ep->syswide_socket, ep->device->base_addr, ep->role);
	// ep->syswide_socket.namespace_index = sys;
	// ep->syswide_socket.belongs_endpoint = ep;
	// ep->syswide_queue = alloc_workqueue("syswide", 0, 0);
	// INIT_WORK(&ep->syswide_work, endpoint_syswide_poll);
	// ep->syswide_work->data = (unsigned long)(&ep->syswide_socket);
	// queue_work(ep->syswide_queue, &ep->syswide_work);
}

static int sys_handle_connect(ringbuf_socket *sock, rbmsg_hd *hd) {
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

static int sys_handle_accept(ringbuf_socket *sock, rbmsg_hd *hd) {
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

static int sys_handle_keepalive(ringbuf_socket *sock, rbmsg_hd *hd) {
	socket->sync_toggle = TRUE;
}

static int sys_handle_disconnect(ringbuf_socket *sock, rbmsg_hd *hd) {
	//TODO
}

static int sys_handle_ack(ringbuf_socket *sock, rbmsg_hd *hd) 
{
	return 0;
}
		
// static int handle_msg_type_req(ringbuf_socket *socket, rbmsg_hd *hd) 
// {
// 	rbmsg_hd new_hd;
// 	char buffer[256];
// 	size_t pld_len;

// 	sprintf(buffer, "msg dst_id=%d src_id=%d - (jiffies: %lu)",
// 			hd->src_qid, dev->ivposition, jiffies);
// 	pld_len = strlen(buffer) + 1;

// 	new_hd.src_qid = NODEID;
// 	new_hd.msg_type = msg_type_add;
// 	new_hd.payload_off = endpoint_add_payload(
// 			(ringbuf_endpoint*)socket->belongs_endpoint, len);
// 	new_hd.payload_len = pld_len;
// 	new_hd.is_sync = 0;

// 	memcpy(((ringbuf_endpoint*)socket->belongs_endpoint)->mem_pool_area
// 					+ new_hd.payload_off, buffer, len);
// 	wmb();
// 	socket_send_async(socket, &new_hd);

// 	return 0;
// }

// static int handle_msg_type_add(ringbuf_socket *socket, rbmsg_hd *hd)
// {
// 	char buffer[256];

// 	memcpy(	buffer, 
// 		((ringbuf_endpoint*)socket->belongs_endpoint)->mem_pool_area
// 			+ hd->payload_off, 
// 		hd->payload_len);
// 	rmb();
	
// 	printk(KERN_INFO "PAYLOAD_CONTENT: %s\n", buffer);

// 	hd->msg_type = msg_type_free;
// 	socket_send_async(socket, hd);
// 	return 0;
// }

// static int handle_msg_type_free(ringbuf_socket *socket, rbmsg_hd *hd)
// {
// 	endpoint_free_payload((ringbuf_endpoint*)socket->belongs_endpoint, hd);
// 	return 0;
// }
