ifneq ($(KERNELRELEASE),)
obj-m	:= ktsdb_socket.o
#ktsdb_socket-y := libleveldb.a 
#obj-y := ktsdb_socket.o
else
KDIR	:= /lib/modules/$(shell uname -r)/build
PWD	:= $(shell pwd)

default:
	$(MAKE)	-C $(KDIR)	M=$(PWD) modules
endif

clean:
	rm -rf *.ko *.o *.mod.c .*.cmd .tmp_versions Module.symvers