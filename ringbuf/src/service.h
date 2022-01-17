#include "socket.h"
#include <linux/list.h>

#define MAX_SERVICE_NUM 8
#define MAX_SOCKET_NUM 64

typedef int (*msg_handler)(ringbuf_socket *sock, rbmsg_hd *hd);

typedef struct service {
	char 			name[64];
	msg_handler 		msg_handlers[MAX_MSG_TYPE];
	ringbuf_socket		*sockets[MAX_SOCKET_NUM];
        struct list_head        service_list;
} service;

static LIST_HEAD(services);

static void register_service(service *serv);
static void unregister_service(service *serv);

static void register_message(service *serv, int msg_type, msg_handler);
static void unregister_message(service *serv, int msg_type);

static void handle_message(service *serv, 
                        ringbuf_socket *sock, rbmsg_hd *hd);