#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/fs.h>  // file_operations
#include <linux/init.h>
#include <linux/jiffies.h>
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
#include "nanomodbus.h"

// Meta Information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emile Decosterd");
MODULE_DESCRIPTION("Simple RTU modbus driver");

int modbus_dev_major = 0;  // use dynamic major
int modbus_dev_minor = 0;

#define BUFFER_LENGTH 256
#define MODBUS_N_REGS 4

// Modbus registers mirror
static uint16_t regs[MODBUS_N_REGS];

// nanomodbus handle
static nmbs_t nmbs;

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
    struct mutex modbus_lock;
    struct cdev cdev;  // Char device structure
};
static struct modbus_device_t modbus_dev;

int32_t read_serial(uint8_t* buf, uint16_t count, int32_t byte_timeout_ms, void* arg)
{
    (void)arg;  // unused

    if (NULL == buf)
    {
        return -EFAULT;
    }

    // Wait until we received something
    unsigned long timestamp_now = jiffies;
    unsigned long timestamp_timeout = timestamp_now + ((byte_timeout_ms * HZ) / 1000);  // no check for overflow, I know
    while (!byte_fifo_is_available(&rx_fifo) && (jiffies < timestamp_timeout))
    {
        msleep(10);
    }
    if (jiffies >= timestamp_timeout)
    {
        return -ETIMEDOUT;
    }

    return byte_fifo_read(&rx_fifo, buf, count);
}

int32_t write_serial(const uint8_t* buf, uint16_t count, int32_t byte_timeout_ms, void* arg)
{
    (void)arg;  // unused

    struct serdev_device* serdev = modbus_dev.serdev;
    if ((NULL == serdev) || (NULL == buf))
    {
        return -EFAULT;
    }

    int status = serdev_device_write_buf(serdev, buf, count);
    printk("serdev_serial - Wrote %d bytes.\n", status);

    return status;
}

nmbs_error init_modbus_client(nmbs_t* nmbs)
{
    nmbs_platform_conf conf;

    nmbs_platform_conf_create(&conf);
    conf.transport = NMBS_TRANSPORT_RTU;
    conf.read = read_serial;
    conf.write = write_serial;

    nmbs_error status = nmbs_client_create(nmbs, &conf);
    if (status != NMBS_ERROR_NONE)
    {
        return status;
    }

    nmbs_set_byte_timeout(nmbs, 100);
    nmbs_set_read_timeout(nmbs, 1000);

    return NMBS_ERROR_NONE;
}

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
    struct modbus_device_t* dev = filp->private_data;
    struct byte_fifo_t* fifo = dev->fifo;

    if ((NULL == dev) || (NULL == fifo))
    {
        return -EFAULT;
    }

    char* kbuffer = kmalloc(BUFFER_LENGTH, GFP_KERNEL);
    if (NULL == kbuffer)
    {
        return -ENOMEM;
    }

    // Dummy response
    sprintf(kbuffer, "Reg[0]=%u\nReg[1]=%u\nReg[2]=%u\nReg[3]=%u", regs[0], regs[1], regs[2], regs[3]);
    printk("Formatted read response: %s", kbuffer);

    if (copy_to_user(buf, kbuffer, strlen(kbuffer)) > 0)
    {
        kfree(kbuffer);
        return -EFAULT;
    }
    kfree(kbuffer);

    return count;
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

    nmbs_error ret = NMBS_ERROR_NONE;
    if (kbuffer[0] == 'r')
    {
        printk("Modbus device - Read registers");
        mutex_lock(&dev->modbus_lock);
        ret = nmbs_read_holding_registers(&nmbs, 0, 4, regs);
        mutex_unlock(&dev->modbus_lock);
    }
    else if (kbuffer[0] == 'w')
    {
        printk("Modbus device - Write registers");
        mutex_lock(&dev->modbus_lock);
        ret = nmbs_write_multiple_registers(&nmbs, 0, 4, regs);
        mutex_unlock(&dev->modbus_lock);
    }
    else
    {
        printk("Modbus device - Unrecognized command");
    }
    kfree(kbuffer);

    if (NMBS_ERROR_NONE != ret)
    {
        printk("Modbus device - Error reading or writing registers");
        return -ENODEV;
    }

    return count;
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
        printk("serdev_serial - Overwrote %d bytes", res);
    }
    else if (res < 0)
    {
        printk("serdev_serial - Error: %d", res);
    }
    else
    {
        printk("serdev_serial - Write %lu bytes", size);
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

    // Here we could read the device identification

    // Store a pointer to the serial device so we can write to it later
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

    printk("Serial Modbus - Loading the serial device driver...\n");
    if (serdev_device_driver_register(&serdev_serial_driver))
    {
        printk("serdev_serial - Error! Could not load serial device driver\n");
        return -1;
    }

    result = alloc_chrdev_region(&dev, modbus_dev_minor, 1, "serial_modbus");
    modbus_dev_major = MAJOR(dev);
    if (result < 0)
    {
        printk(KERN_WARNING "Can't get major %d\n", modbus_dev_major);
        return result;
    }

    memset(&modbus_dev, 0, sizeof(struct modbus_device_t));
    byte_fifo_init(&rx_fifo);
    modbus_dev.fifo = &rx_fifo;
    mutex_init(&modbus_dev.modbus_lock);

    memset(&regs, 0, sizeof(regs));
    nmbs_error status = init_modbus_client(&nmbs);
    if (NMBS_ERROR_NONE != status)
    {
        printk("Serial Modbus - Error initializing nanomodbus");
        return -ENODEV;
    }
    nmbs_set_destination_rtu_address(&nmbs, 0x01);

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
