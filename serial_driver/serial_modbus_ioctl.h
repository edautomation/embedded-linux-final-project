#ifndef SERIAL_MODBUS_IOCTL_H
#define SERIAL_MODBUS_IOCTL_H

#ifdef __KERNEL__
#include <asm-generic/ioctl.h>
#include <linux/types.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
#endif

// Pick an arbitrary unused value from https://github.com/torvalds/linux/blob/master/Documentation/userspace-api/ioctl/ioctl-number.rst
#define SERIAL_MODBUS_IOC_MAGIC 0x16

// Define a write command from the user point of view, use command number 1
#define SERIAL_MODBUSCHAR_IOCSETADDR _IOWR(SERIAL_MODBUS_IOC_MAGIC, 1, unsigned long)

#endif /* SERIAL_MODBUS_IOCTL_H */