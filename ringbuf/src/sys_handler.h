#include "endpoint.h"
#include "service.h"
#include "sys_msgtype.h"
#include "ringbuf.h"

static void init_systemwide_service();
static void register_sys_handlers();
static void set_system_socket(ringbuf_endpoint *ep);

/* message handlers: sys namespace */
static int sys_handle_connect(ringbuf_socket *sock, rbmsg_hd *hd);
static int sys_handle_accept(ringbuf_socket *sock, rbmsg_hd *hd);
static int sys_handle_keepalive(ringbuf_socket *sock, rbmsg_hd *hd);
static int sys_handle_disconnect(ringbuf_socket *sock, rbmsg_hd *hd);
static int sys_handle_ack(ringbuf_socket *sock, rbmsg_hd *hd);

