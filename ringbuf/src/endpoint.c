#include "endpoint.h"

static ringbuf_socket* endpoint_create_socket(ringbuf_endpoint *ep, 
					int nsp_index, char *socket_name) {
	int i;
	ringbuf_socket *socket;
	unsigned long buffer_offset;

	for(i = 0; i < MAX_SOCKET_NUM; i++) {
		if(ep->sockets[i].in_use == FALSE)
			break;
	}
	socket = ep->sockets + i;

	socket->in_use = TRUE;
	socket->belongs_endpoint = ep;
	strcpy(socket->name, socket_name);
	socket->namespace_index = nsp_index;
	socket->listening = FALSE;
	
	socket->sync_toggle = 0;
	if(ep->role == Host) {
		buffer_offset = endpoint_add_payload(ep, sizeof(struct pcie_buffer));
		socket_bind(socket, ep->device->base_addr + buffer_offset, ep->role);
	}
	
	return socket;
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

static void endpoint_register_msg_handler(ringbuf_endpoint *ep,
			int nsp_index, int msg_type, msg_handler handler) {
	if(handler == NULL || msg_type > MAX_MSG_TYPE || msg_type <= 0) {
		return;
	}
	if(nsp_index > MAX_NAMESPACE_NUM || nsp_index < 0){
		return;
	}
	ep->namespaces[nsp_index].msg_handlers[msg_type] = handler;
}

static void endpoint_unregister_msg_handler(ringbuf_endpoint *ep, 
					int nsp_index, int msg_type) {
	if(handler == NULL || msg_type > MAX_MSG_TYPE || msg_type <= 0) {
		return;
	}
	if(nsp_index > MAX_NAMESPACE_NUM || nsp_index < 0){
		return;
	}
	ep->namespaces[nsp_index].msg_handlers[msg_type] = NULL;
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
