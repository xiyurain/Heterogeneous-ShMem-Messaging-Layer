CONFIG_MODULE_SIG=4

obj-m += ivpci.o
KERNELDIR ?= /home/popcorn/kernel_src/linux-5.15.1/
all:
	make -C $(KERNELDIR) M=$(PWD) modules
	
clean:
	make -C $(KERNELDIR) M=$(PWD) clean
