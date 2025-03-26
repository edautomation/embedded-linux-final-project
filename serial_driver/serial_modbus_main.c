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
#include "serial_modbus_ioctl.h"

// Meta Information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emile Decosterd");
MODULE_DESCRIPTION("Simple RTU modbus driver");

int modbus_dev_major = 0;  // use dynamic major
int modbus_dev_minor = 0;

#define BUFFER_LENGTH 256
#define UINT16_MAX    65535
#define INT32_MAX     2147483647

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

// Private file data
struct modbus_handle_t
{
    uint16_t start_address;
    struct modbus_device_t* dev;
};

int32_t read_serial(uint8_t* buf, uint16_t count, int32_t byte_timeout_ms, void* arg)
{
    (void)arg;  // unused

    if (NULL == buf)
    {
        return -EFAULT;
    }

    // Check for overflow and compute timeout
    printk("nanomodbus - Time to receive %u bytes: %ld", count, byte_timeout_ms);
    if (byte_timeout_ms >= (INT32_MAX / HZ))
    {
        return -EINVAL;
    }
    unsigned long timestamp_now = jiffies;
    unsigned long timestamp_timeout = timestamp_now + ((byte_timeout_ms * HZ) / 1000);

    // Get data from queue. It might be filled asynchronously, so keep reading until
    // all expected bytes were read or a timeout occured.
    uint16_t read_bytes = 0;
    while ((read_bytes < count) && (jiffies < timestamp_timeout))
    {
        uint16_t bytes_left_to_read = count - read_bytes;
        int16_t res = byte_fifo_read(&rx_fifo, buf, bytes_left_to_read);
        if (res >= 0)
        {
            read_bytes += res;
        }
        else
        {
            printk("nanodmodbus - Error reading bytes from fifo: %d", res);
            return -EFAULT;
        }
    }

    // Result check
    unsigned long timestamps_stop = jiffies;
    if (timestamps_stop > timestamp_timeout)
    {
        printk("nanomodbus - Read serial timed out (read %d bytes). Timestamp start: %lu, timestamp stop: %lu", read_bytes, timestamp_now, timestamps_stop);
        return -ETIMEDOUT;
    }

    return (int32_t)read_bytes;
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
    printk("nanomodbus - Wrote %d bytes.\n", status);

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
    struct modbus_handle_t* modbus_handle = NULL;

    printk("Open modbus char device");

    // Our device is global and persistent -> no need to do any particular device handling.
    // Only store a pointer to our device in the file structure for ease of access

    // NOTE: struct file represents a file descriptor, whereas struct inode represents the file
    // itself => there can be multiple struct file representing multiple open descriptors
    // on a single file, but they all point to the same inode structure.
    dev = container_of(inode->i_cdev, struct modbus_device_t, cdev);

    modbus_handle = kmalloc(sizeof(struct modbus_handle_t), GFP_KERNEL);
    if (NULL == modbus_handle)
    {
        return -ENOMEM;
    }

    // Each "file" will have a different start address for read/write operations
    modbus_handle->start_address = 0;
    modbus_handle->dev = dev;  // store a pointer to our global device
    filp->private_data = modbus_handle;

    return 0;
}

int modbus_dev_release(struct inode* inode, struct file* filp)
{
    printk("Modbus Device Release");

    kfree(filp->private_data);  // Release the data structure created in the open function

    return 0;
}

ssize_t modbus_dev_read(struct file* filp, char __user* buf, size_t count, loff_t* f_pos)
{
    struct modbus_handle_t* handle = filp->private_data;
    struct modbus_device_t* dev = handle->dev;

    if ((NULL == dev) || (NULL == buf))
    {
        return -EFAULT;
    }

    // Check parameters
    const uint16_t start_addr = handle->start_address;
    const size_t n_regs = count / 2;           // Modbus registers are 16-bit, count is the number of bytes
    if ((start_addr + n_regs > UINT16_MAX) ||  // Modbus address space limit
        (n_regs > 125))                        // max 250 bytes of data per transaction
    {
        printk("Modbus device - Invalid parameters for read (start address or count)");
        return -EINVAL;
    }

    const size_t buffer_size = n_regs * sizeof(uint16_t);
    uint16_t* kbuffer = kmalloc(buffer_size, GFP_KERNEL);
    if (NULL == kbuffer)
    {
        return -ENOMEM;
    }

    // Actually read from the device
    mutex_lock(&dev->modbus_lock);
    nmbs_error err = nmbs_read_holding_registers(&nmbs, start_addr, n_regs, kbuffer);
    mutex_unlock(&dev->modbus_lock);
    if (NMBS_ERROR_NONE != err)
    {
        printk("Modbus device - Could not read holding registers. Error: %d", err);
        kfree(kbuffer);
        return -EIO;
    }

    // Get data back to user space
    if (copy_to_user(buf, kbuffer, buffer_size) > 0)
    {
        printk("Modbus device - Could not copy from user space!");
        kfree(kbuffer);
        return -EFAULT;
    }

    kfree(kbuffer);
    return count;  // Here we read everything at once
}

ssize_t modbus_dev_write(struct file* filp, const char __user* buf, size_t count, loff_t* f_pos)
{
    struct modbus_handle_t* handle = filp->private_data;
    struct modbus_device_t* dev = handle->dev;

    if ((NULL == dev) || (NULL == buf))
    {
        return -EFAULT;
    }

    // Check parameters
    const uint16_t start_addr = handle->start_address;
    const size_t n_regs = count / 2;           // Modbus registers are 16-bit, count is the number of bytes
    if ((start_addr + n_regs > UINT16_MAX) ||  // Modbus address space limit
        (n_regs > 125))                        // max 250 bytes of data per transaction
    {
        printk("Modbus device - Invalid parameters for write (start address or count)");
        return -EINVAL;
    }

    const size_t buffer_size = n_regs * sizeof(uint16_t);
    uint16_t* kbuffer = kmalloc(buffer_size, GFP_KERNEL);
    if (NULL == kbuffer)
    {
        return -ENOMEM;
    }

    // We will manipulate memory in the kernel space
    if (copy_from_user(kbuffer, buf, buffer_size))
    {
        printk("Modbus device - Could not copy from user space!");
        kfree(kbuffer);
        return -EFAULT;
    }

    mutex_lock(&dev->modbus_lock);
    nmbs_error err = nmbs_write_multiple_registers(&nmbs, start_addr, n_regs, kbuffer);
    mutex_unlock(&dev->modbus_lock);

    kfree(kbuffer);

    if (NMBS_ERROR_NONE != err)
    {
        printk("Modbus device - Error writing registers: %d", err);
        return -ENODEV;
    }

    return count;  // Here we wrote everything we wanted
}

long int modbus_dev_ioctl(struct file* filp, unsigned int cmd, unsigned long arg)
{
    struct modbus_handle_t* handle = filp->private_data;
    unsigned long new_address = 0;

    // Check if command is supported
    if (SERIAL_MODBUSCHAR_IOCSETADDR != cmd)
    {
        return -ENOTTY;
    }

    // We are working in the kernel space -> need to copy memory
    if (copy_from_user(&new_address, (void __user*)arg, sizeof(unsigned long)))
    {
        return -EFAULT;
    }

    // Sanity check of provided value
    if (new_address >= UINT16_MAX)
    {
        return -EINVAL;
    }

    // Actual purpose of the ioctl call
    handle->start_address = new_address;

    return 0;
}

struct file_operations modbus_dev_fops = {
    .owner = THIS_MODULE,
    .read = modbus_dev_read,
    .write = modbus_dev_write,
    .unlocked_ioctl = modbus_dev_ioctl,
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
    printk("serdev_serial - Received %u bytes \n", size);

    int res = byte_fifo_write(&rx_fifo, buffer, size);
    if (res > 0)
    {
        printk("serdev_serial - Overwrote %d bytes in fifo", res);
    }
    else if (res < 0)
    {
        printk("serdev_serial - Error: %d", res);
    }
    else
    {
        printk("serdev_serial - Write %u bytes to fifo", size);
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
