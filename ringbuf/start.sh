ivshmem-server -l 4M -M fg-doorbell -n 8 -F -v

ivshmem-client
dump

qemu-system-x86_64 -m 1024M \
    -drive format=raw,file=rootfs.img \
    -kernel ../images/bzImage \
    -append "root=/dev/sda init=/bin/ash" \
    -chardev socket,path=/tmp/ivshmem_socket,id=fg-doorbell \
    -device ivshmem-doorbell,chardev=fg-doorbell,vectors=8
    