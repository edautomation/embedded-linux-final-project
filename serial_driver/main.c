#include <linux/cdev.h>
#include <linux/fs.h>  // file_operations
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/property.h>
#include <linux/serdev.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include "byte_fifo.h"

// Meta Information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emile Decosterd");
MODULE_DESCRIPTION("Simple char driver using a serial device");

int modbus_dev_major = 0;  // use dynamic major
int modbus_dev_minor = 0;

#define BUFFER_LENGTH 256

// Synchronization fifo
static unsigned char rx_buffer[BUFFER_LENGTH];
static struct byte_fifo_t rx_fifo = {
    .data = rx_buffer,
    .size = BUFFER_LENGTH,
};

// Our driver object
struct modbus_device_t
{
    struct byte_fifo_t* fifo;
    struct serdev_device* serdev;
    struct cdev cdev;  // Char device structure
};
static struct modbus_device_t modbus_dev;

int modbus_dev_open(struct inode* inode, struct file* filp)
{
    struct modbus_device_t* dev = NULL;

    printk("Open modbus char device");

    // Our device is global and persistent -> no need to do any particular device handling.
    // Only store a pointer to our device in the file structure for ease of access

    // NOTE: struct file represents a file descriptor, whereas struct inode represents the file
    // itself => there can be multiple struct file representing multiple open descriptors
    // on a single file, but they all point to the same inode structure.
    dev = container_of(inode->i_cdev, struct modbus_device_t, cdev);
    filp->private_data = dev;  // store a pointer to our global device

    return 0;
}

int modbus_dev_release(struct inode* inode, struct file* filp)
{
    printk("Modbus Device Release");

    // Nothing to do here
    return 0;
}

ssize_t modbus_dev_read(struct file* filp, char __user* buf, size_t count, loff_t* f_pos)
{
    struct modbus_device_t* dev = NULL;
    struct byte_fifo_t* fifo = NULL;
    dev = filp->private_data;
    fifo = dev->fifo;

    if ((NULL == dev) || (NULL == fifo))
    {
        return -EFAULT;
    }

    char* kbuffer = kmalloc(count, GFP_KERNEL);
    if (NULL == kbuffer)
    {
        return -ENOMEM;
    }

    int read_bytes = byte_fifo_read(fifo, kbuffer, count);
    int res = copy_to_user(buf, kbuffer, read_bytes);

    kfree(kbuffer);
    return res;
}

ssize_t modbus_dev_write(struct file* filp, const char __user* buf, size_t count, loff_t* f_pos)
{
    struct modbus_device_t* dev = dev = filp->private_data;
    struct serdev_device* serdev = serdev = dev->serdev;

    if ((NULL == dev) || (NULL == serdev))
    {
        return -EFAULT;
    }

    char* kbuffer = kmalloc(count, GFP_KERNEL);
    if (NULL == kbuffer)
    {
        printk("Modbus device - Could not allocate memory");
        return -ENOMEM;
    }

    // We will manipulate memory in the kernel space
    if (copy_from_user(kbuffer, buf, count))
    {
        printk("Modbus device - Could not copy from user space!");
        kfree(kbuffer);
        return -EFAULT;
    }

    int status = serdev_device_write_buf(serdev, kbuffer, count);
    printk("serdev_serial - Wrote %d bytes.\n", status);
    kfree(kbuffer);

    return status;
}

struct file_operations modbus_dev_fops = {
    .owner = THIS_MODULE,
    .read = modbus_dev_read,
    .write = modbus_dev_write,
    // .llseek = modbus_dev_seek,
    // .unlocked_ioctl = modbus_dev_ioctl,
    .open = modbus_dev_open,
    .release = modbus_dev_release,
};

static int modbus_dev_setup_cdev(struct modbus_device_t* dev)
{
    int err, devno = MKDEV(modbus_dev_major, modbus_dev_minor);

    cdev_init(&dev->cdev, &modbus_dev_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &modbus_dev_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
    {
        printk(KERN_ERR "Error %d adding modbus_dev cdev", err);
    }
    return err;
}

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

    int res = byte_fifo_write(&rx_fifo, buffer, size);
    if (res > 0)
    {
        printk("serdev_serial - Overwrote %d bytes.", res);
    }

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

    modbus_dev.serdev = serdev;

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
    dev_t dev = 0;
    int result = 0;
    result = alloc_chrdev_region(&dev, modbus_dev_minor, 1, "modbus_dev_char");
    modbus_dev_major = MAJOR(dev);
    if (result < 0)
    {
        printk(KERN_WARNING "Can't get major %d\n", modbus_dev_major);
        return result;
    }

    printk("Serial Modbus - Loading the serial device driver...\n");
    if (serdev_device_driver_register(&serdev_serial_driver))
    {
        printk("serdev_serial - Error! Could not load serial device driver\n");
        return -1;
    }

    memset(&modbus_dev, 0, sizeof(struct modbus_device_t));
    byte_fifo_init(&rx_fifo);
    modbus_dev.fifo = &rx_fifo;

    result = modbus_dev_setup_cdev(&modbus_dev);
    if (result)
    {
        printk("Serial Modbus - Error setting up device");
        unregister_chrdev_region(dev, 1);
    }

    return result;
}

/**
 * @brief This function is called when the module is removed from the kernel
 */
static void __exit my_exit(void)
{
    printk("Serial Modbus - Unload driver");
    serdev_device_driver_unregister(&serdev_serial_driver);

    dev_t devno = MKDEV(modbus_dev_major, modbus_dev_minor);

    cdev_del(&modbus_dev.cdev);

    /**
     * TODO: cleanup modbus_dev specific portions here as necessary
     */

    unregister_chrdev_region(devno, 1);
}

module_init(my_init);
module_exit(my_exit);
