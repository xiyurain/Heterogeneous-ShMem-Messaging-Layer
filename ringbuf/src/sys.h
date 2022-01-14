#include "endpoint.h"

/* message handlers: sys namespace */
static int handle_sys_conn(ringbuf_socket *socket, rbmsg_hd *hd);
static int handle_sys_accept(ringbuf_socket *socket, rbmsg_hd *hd);
static int handle_sys_kalive(ringbuf_socket *socket, rbmsg_hd *hd);
static int handle_sys_disconn(ringbuf_socket *socket, rbmsg_hd *hd);
static int handle_sys_ack(ringbuf_socket *socket, rbmsg_hd *hd);
static int handle_sys_req(ringbuf_socket *socket, rbmsg_hd *hd);
static int handle_sys_add(ringbuf_socket *socket, rbmsg_hd *hd);
static int handle_sys_free(ringbuf_socket *socket, rbmsg_hd *hd);

static int register_sys_handlers(ringbuf_endpoint *ep);