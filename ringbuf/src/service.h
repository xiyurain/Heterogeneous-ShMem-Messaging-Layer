#include "socket.h"

typedef int (*msg_handler)(ringbuf_socket *socket, rbmsg_hd *hd);

typedef struct service {
	char 			name[64];
	msg_handler 		msg_handlers[MAX_MSG_TYPE];
	ringbuf_socket		*sockets[MAX_SOCKET_NUM];
} service;

static service services[MAX_SERVICE_NUM];
