qemu-system-x86_64 -m 4096 \
	#-smp 4 -M q35 \
	-nographic \
	-kernel /home/popcorn/images/bzImage \
	-initrd /home/popcorn/images/initrd.img \
	-append "root=/dev/ram rw rdinit=/linuxrc console=ttyS0"
