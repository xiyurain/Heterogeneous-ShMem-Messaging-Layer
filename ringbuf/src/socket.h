#include "endpoint.h"
#include "sys_msgtype.h"

typedef struct ringbuf_socket {
	char 			name[64];
	char 			service[16];

	int 			is_listening;
	ringbuf_endpoint	*bind_endpoint;
	pcie_port		*bind_port;

	int			sync_toggle;
	struct timer_list 	keep_alive_timer;
} ringbuf_socket;

/*API of the ringbuf socket*/
static ringbuf_socket *socket_create(char *name, char *service);
static void socket_bind(ringbuf_socket *sock, 
		unsigned int remote_id, int role, unsigned long buf_addr);
static void socket_listen(ringbuf_socket *sock);
static void socket_connect(ringbuf_socket *sock);
static void socket_accept(ringbuf_socket *sock, rbmsg_hd *hd);
static void socket_send_sync(ringbuf_socket *sock, rbmsg_hd *hd);
static void socket_send_async(ringbuf_socket *sock, rbmsg_hd *hd);
static void socket_receive(ringbuf_socket *sock, rbmsg_hd *hd);
static int socket_keepalive(ringbuf_socket *sock);
static void socket_disconnect(ringbuf_socket *sock);