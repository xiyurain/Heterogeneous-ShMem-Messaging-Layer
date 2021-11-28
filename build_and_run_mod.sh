#!/bin/bash

cd ./ringbuf/src && make

cd ../../fs/

rm -r ./rootfs/
mkdir ./rootfs
mount -o loop rfs.img ./rootfs
rm -r rootfs/bin/ringbuf/
cp -r ../ringbuf/ rootfs/bin
umount rootfs
rm -r ./rootfs/
cp rfs.img rfs2.img

# gdb --args /home/qemu/build/qemu-system-x86_64 -m 1024M \
# /home/qemu/build/qemu-system-x86_64 -m 1024M \
qemu-system-x86_64 -m 1024M \
    -drive format=raw,file=rfs.img \
    -kernel ../images/bzImage \
    -append "root=/dev/sda init=/bin/ash" \
    -chardev socket,path=/tmp/ivshmem_socket,id=fg-doorbell \
    -device ivshmem-doorbell,chardev=fg-doorbell,vectors=4