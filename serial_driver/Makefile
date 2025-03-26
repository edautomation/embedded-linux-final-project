# See example Makefile from scull project
# Comment/uncomment the following line to disable/enable debugging
DEBUG = y

# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
  DEBFLAGS = -O -g -DSERIAL_MODBUS_DEBUG -DNMBS_SERVER_DISABLED# "-O" is needed to expand inlines
else
  DEBFLAGS = -O2
endif

EXTRA_CFLAGS += $(DEBFLAGS)

ifneq ($(KERNELRELEASE),)
# call from kernel build system
obj-m	:= serial_modbus.o
serial_modbus-y := byte_fifo.o nanomodbus.o serial_modbus_main.o
ccflags-y := -std=gnu99 -Wno-declaration-after-statement -Wno-vla
else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions
