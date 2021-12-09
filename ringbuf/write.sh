insmod /bin/ringbuf/r1/ringbuf_r1.ko ROLE=0 PCIID=4 DEVNAME=ringbuf_r1
insmod /bin/ringbuf/r2/ringbuf_r2.ko ROLE=0 PCIID=5 DEVNAME=ringbuf_r2
insmod /bin/ringbuf/w1/ringbuf_w1.ko ROLE=1 PCIID=6 DEVNAME=ringbuf_w1
insmod /bin/ringbuf/w2/ringbuf_w2.ko ROLE=1 PCIID=7 DEVNAME=ringbuf_w2
mknod /dev/ringbuf c 246 0
mknod /dev/ringbuf2 c 245 0
insmod /bin/ringbuf/test/send_msg.ko
insmod /bin/ringbuf/test2/send_msg2.ko