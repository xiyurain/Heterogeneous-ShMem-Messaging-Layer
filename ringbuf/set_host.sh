insmod /bin/ringbuf/src/ringbuf.ko NODEID=$1
mknod /dev/ringbuf c 248 0