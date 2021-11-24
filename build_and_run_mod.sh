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
