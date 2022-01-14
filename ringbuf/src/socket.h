#include "endpoint.h"

typedef struct ringbuf_socket {
	char 			name[64];
	int 			in_use;
	int 			listening;
	int 			service_index;

	pcie_port		*bind_port;
	ringbuf_endpoint	*bind_endpoint;

	int			sync_toggle;
	struct timer_list 	keep_alive_timer;
} ringbuf_socket;

/*API of the ringbuf socket*/
static void socket_bind(ringbuf_socket *socket, unsigned long addr, int role);
static void socket_listen(ringbuf_endpoint *ep, ringbuf_socket *socket);
static void socket_connect(ringbuf_endpoint *ep, ringbuf_socket *socket);
static void socket_send_sync(ringbuf_socket *socket, rbmsg_hd *hd);
static void socket_send_async(ringbuf_socket *socket, rbmsg_hd *hd);
static void socket_receive(ringbuf_socket *socket, rbmsg_hd *hd);
static void socket_disconnect(ringbuf_socket *socket);
static int socket_keepalive(ringbuf_socket *socket);