insmod /bin/ringbuf/r1/ringbuf_r1.ko ROLE=0 NODEID=$1
mknod /dev/ringbuf c 248 0
insmod /bin/ringbuf/test/send_msg.ko
