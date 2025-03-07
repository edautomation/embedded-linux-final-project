#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/serdev.h>

#include "byte_fifo.h"

// Meta Information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emile Decosterd");
MODULE_DESCRIPTION("Simple char driver using a serial device");

#define BUFFER_LENGTH 256

// Synchronization fifo
static unsigned char rx_buffer[BUFFER_LENGTH];
static struct byte_fifo_t rx_fifo = {
    .data = rx_buffer,
    .size = BUFFER_LENGTH,
};

/* Declate the probe and remove functions */
static int serdev_serial_probe(struct serdev_device* serdev);
static void serdev_serial_remove(struct serdev_device* serdev);

static struct of_device_id serdev_serial_ids[] = {
    {
        .compatible = "serialdev",  // overlay device name must be serialdev
    },
    {/* sentinel */}};
MODULE_DEVICE_TABLE(of, serdev_serial_ids);

static struct serdev_device_driver serdev_serial_driver = {
    .probe = serdev_serial_probe,
    .remove = serdev_serial_remove,
    .driver = {
        .name = "serdev-serial",
        .of_match_table = serdev_serial_ids,
    },
};

// Callback is called whenever a character is received
static int serdev_serial_recv(struct serdev_device* serdev, const unsigned char* buffer, size_t size)
{
    printk("serdev_serial - Received %ld bytes with \"%s\"\n", size, buffer);

    // TODO : mutex
    (void)byte_fifo_write(&rx_fifo, buffer, size);

    return size;
}

static const struct serdev_device_ops serdev_serial_ops = {
    .receive_buf = serdev_serial_recv,
};

/**
 * @brief This function is called on loading the driver
 */
static int serdev_serial_probe(struct serdev_device* serdev)
{
    int status;
    printk("serdev_serial - Now I am in the probe function!\n");

    serdev_device_set_client_ops(serdev, &serdev_serial_ops);
    status = serdev_device_open(serdev);
    if (status)
    {
        printk("serdev_serial - Error opening serial port!\n");
        return -status;
    }

    serdev_device_set_baudrate(serdev, 115200);
    serdev_device_set_flow_control(serdev, false);
    serdev_device_set_parity(serdev, SERDEV_PARITY_NONE);

    status = serdev_device_write_buf(serdev, "Hello, from serdev", sizeof("Hello, from serdev"));
    printk("serdev_serial - Wrote %d bytes.\n", status);

    return 0;
}

/**
 * @brief This function is called on unloading the driver
 */
static void serdev_serial_remove(struct serdev_device* serdev)
{
    printk("serdev_serial - Now I am in the remove function\n");
    serdev_device_close(serdev);
}

/**
 * @brief This function is called when the module is loaded into the kernel
 */
static int __init my_init(void)
{
    printk("serdev_serial - Loading the driver...\n");
    if (serdev_device_driver_register(&serdev_serial_driver))
    {
        printk("serdev_serial - Error! Could not load driver\n");
        return -1;
    }

    byte_fifo_init(&rx_fifo);

    return 0;
}

/**
 * @brief This function is called when the module is removed from the kernel
 */
static void __exit my_exit(void)
{
    printk("serdev_serial - Unload driver");
    serdev_device_driver_unregister(&serdev_serial_driver);
}

module_init(my_init);
module_exit(my_exit);
