#!/bin/bash

cd ./ringbuf/r1 && cp ../src/ringbuf.c ./ringbuf_r1.c && make
cd        ../r2 && cp ../src/ringbuf.c ./ringbuf_r2.c && make
cd        ../w1 && cp ../src/ringbuf.c ./ringbuf_w1.c && make
cd        ../w2 && cp ../src/ringbuf.c ./ringbuf_w2.c && make
cd        ../test/                                    && make
cd        ../test2/                                   && make

cd ../../fs/

rm -r ./rootfs/
mkdir ./rootfs
mount -o loop rfs.img ./rootfs
rm -r rootfs/bin/ringbuf/
cp -rp ../ringbuf/ rootfs/bin
umount rootfs
rm -r ./rootfs/
cp rfs.img rfs2.img
cp rfs.img rfs3.img
# cp rfs.img rfs4.img


# gdb --args /home/qemu/build/qemu-system-x86_64 -m 1024M \
# /home/qemu/build/qemu-system-x86_64 -m 1024M \
nohup qemu-system-x86_64 -m 1024M \
    -drive format=raw,file=rfs.img \
    -kernel ../images/bzImage \
    -append "root=/dev/sda init=/bin/ash" \
    -chardev socket,path=/tmp/ivshmem_socket-10,id=fg-doorbell-10 \
    -chardev socket,path=/tmp/ivshmem_socket-20,id=fg-doorbell-20 \
    -chardev socket,path=/tmp/ivshmem_socket-01,id=fg-doorbell-01 \
    -chardev socket,path=/tmp/ivshmem_socket-02,id=fg-doorbell-02 \
    -device ivshmem-doorbell,chardev=fg-doorbell-10,vectors=4 \
    -device ivshmem-doorbell,chardev=fg-doorbell-20,vectors=4 \
    -device ivshmem-doorbell,chardev=fg-doorbell-01,vectors=4 \
    -device ivshmem-doorbell,chardev=fg-doorbell-02,vectors=4 \
    > qemu.log1 2>&1 &

nohup qemu-system-x86_64 -m 1024M \
    -drive format=raw,file=rfs2.img \
    -kernel ../images/bzImage \
    -append "root=/dev/sda init=/bin/ash" \
    -chardev socket,path=/tmp/ivshmem_socket-01,id=fg-doorbell-01 \
    -chardev socket,path=/tmp/ivshmem_socket-21,id=fg-doorbell-21 \
    -chardev socket,path=/tmp/ivshmem_socket-10,id=fg-doorbell-10 \
    -chardev socket,path=/tmp/ivshmem_socket-12,id=fg-doorbell-12 \
    -device ivshmem-doorbell,chardev=fg-doorbell-01,vectors=4 \
    -device ivshmem-doorbell,chardev=fg-doorbell-21,vectors=4 \
    -device ivshmem-doorbell,chardev=fg-doorbell-10,vectors=4 \
    -device ivshmem-doorbell,chardev=fg-doorbell-12,vectors=4 \
    > qemu.log2 2>&1 &

nohup qemu-system-x86_64 -m 1024M \
    -drive format=raw,file=rfs3.img \
    -kernel ../images/bzImage \
    -append "root=/dev/sda init=/bin/ash" \
    -chardev socket,path=/tmp/ivshmem_socket-02,id=fg-doorbell-02 \
    -chardev socket,path=/tmp/ivshmem_socket-12,id=fg-doorbell-12 \
    -chardev socket,path=/tmp/ivshmem_socket-20,id=fg-doorbell-20 \
    -chardev socket,path=/tmp/ivshmem_socket-21,id=fg-doorbell-21 \
    -device ivshmem-doorbell,chardev=fg-doorbell-02,vectors=4 \
    -device ivshmem-doorbell,chardev=fg-doorbell-12,vectors=4 \
    -device ivshmem-doorbell,chardev=fg-doorbell-20,vectors=4 \
    -device ivshmem-doorbell,chardev=fg-doorbell-21,vectors=4 \
    > qemu.log2 2>&1 &

# nohup qemu-system-x86_64 -m 1024M \
#     -drive format=raw,file=rfs4.img \
#     -kernel ../images/bzImage \
#     -append "root=/dev/sda init=/bin/ash" \
#     -chardev socket,path=/tmp/ivshmem_socket,id=fg-doorbell \
#     -device ivshmem-doorbell,chardev=fg-doorbell,vectors=4 \
#     > qemu.log4 2>&1 &

# ivshmem-client