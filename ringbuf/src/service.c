#include "service.h"

static inline void register_service(service *serv) 
{
        list_add_tail(serv->service_list, services);
}

static inline void unregister_service(service *serv) 
{
        list_del(serv->service_list);
}

static void register_message(service *serv, int msg_type, msg_handler)
{
        if(handler == NULL || msg_type > MAX_MSG_TYPE || msg_type <= 0)
		return;
        serv->msg_handlers[msg_type] = msg_handler;
}

static void unregister_message(service *serv, int msg_type);
{
        if(msg_type > MAX_MSG_TYPE || msg_type <= 0)
		return;
        serv->msg_handlers[msg_type] = NULL;
}

static void handle_message(service *serv, 
                        ringbuf_socket *sock, rbmsg_hd *hd)
{
        msg_handler handler = serv->msg_handlers[hd->msg_type];
        handler(sock, hd);
}