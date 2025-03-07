# See example Makefile from scull project
# Comment/uncomment the following line to disable/enable debugging
DEBUG = y

# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
  DEBFLAGS = -O -g -DSERIAL_DRIVER_DEBUG # "-O" is needed to expand inlines
else
  DEBFLAGS = -O2
endif

EXTRA_CFLAGS += $(DEBFLAGS) -std=gnu99

ifneq ($(KERNELRELEASE),)
# call from kernel build system
obj-m	:= serial_driver.o
serial_driver-y := byte_fifo.o main.o
ccflags-y :=  -Wno-declaration-after-statement
else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

all: modules dt

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

dt: serdev_overlay.dts
	dtc -@ -I dts -O dtb -o serdev_overlay.dtbo serdev_overlay.dts

endif

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions
	rm -rf serdev_overlay.dtbo