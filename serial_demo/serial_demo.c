#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../serial_driver/serial_modbus_ioctl.h"

#define DEBUG
#ifdef DEBUG
#define LOG_DEBUG(...) printf(__VA_ARGS__)
#else
#define LOG_DEBUG(...)
#endif

#define nDUMMY_DRIVER

int fd = 0;

static inline void cleanup(void)
{
    if (0 != fd)
    {
        close(fd);
    }
}

static inline void terminate_normally(void)
{
    LOG_DEBUG("Bye bye!\n");
    cleanup();
    exit(EXIT_SUCCESS);
}

static inline void terminate_with_error(void)
{
    LOG_DEBUG("Ouch, got to abort!\n");
    cleanup();
    exit(EXIT_FAILURE);
}

static void handle_signal(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        LOG_DEBUG("\nGot SIGINT or SIGTERM\n");
        terminate_normally();
    }
}

static void print_buffer(uint16_t* buf, size_t length)
{
    if (NULL == buf)
    {
        printf("Got NULL as buf, are you kidding me?\n");
        return;
    }

    printf("{");

    size_t i = 0;
    for (; i < length - 1; i++)
    {
        printf("%04x, ", buf[i]);
    }
    printf("%04x}\n", buf[i]);
}

void read_from_modbus(int fd, uint16_t* buf, size_t n_regs)
{
    size_t n_bytes = n_regs * sizeof(uint16_t);
    int res = read(fd, buf, n_bytes);
    if (res < 0)
    {
        printf("ERR - Could not read from modbus driver: %d\n", res);
    }
    else
    {
        printf("INFO - Read %d bytes from modbus driver: \n", res);
        print_buffer(buf, n_regs);
    }
}

void write_to_modbus(int fd, uint16_t* buf, size_t n_regs)
{
    size_t n_bytes = n_regs * sizeof(uint16_t);
    int res = write(fd, buf, n_bytes);
    if (res < 0)
    {
        printf("ERR - Could not write to modbus driver: %d\n", res);
    }
    else
    {
        printf("INFO - Wrote %d bytes to modbus driver: \n", res);
    }
}

void set_modbus_address(int fd, unsigned long address)
{
    int res = ioctl(fd, SERIAL_MODBUSCHAR_IOCSETADDR, &address);
    if (res < 0)
    {
        printf("ERR - Could not set modbus address %lu. Reason: %d\n", address, res);
    }
    else
    {
        printf("INFO - Set modbus address to %lu\n", address);
    }
}

int main(int argc, char* argv[])
{
    LOG_DEBUG("********* Modbus serial demo *********\n");

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

#ifndef DUMMY_DRIVER
    fd = open("/dev/serial_modbus", O_RDWR);
#else
    fd = open("/var/tmp/dummy_modbus_drv.txt", O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
#endif
    if (fd < 0)
    {
        printf("ERR - Could not open serial modbus driver\n");
        return EXIT_FAILURE;
    }

    const size_t n_buf_len = 64;
    uint16_t buf[n_buf_len];
    memset(buf, 0, n_buf_len * sizeof(uint16_t));

    LOG_DEBUG(">> Reading 16 registers...\n");
    size_t n_regs = 16;
    read_from_modbus(fd, buf, n_regs);
    LOG_DEBUG("Done! \n");

    LOG_DEBUG(">> Writing 4 registers... \n");
    memset(buf, 0, n_buf_len * sizeof(uint16_t));  // reset previous read
    n_regs = 4;
    buf[0] = 0xF0C1;
    buf[1] = 0xF0C2;
    buf[2] = 0xF0C3;
    buf[3] = 0xF0C4;
    write_to_modbus(fd, buf, n_regs);
    LOG_DEBUG("Done! \n");

    LOG_DEBUG(">> Reading 16 registers... \n");
    n_regs = 16;
    read_from_modbus(fd, buf, n_regs);
    LOG_DEBUG("Done! \n");

    LOG_DEBUG(">> Setting address to the 5th register.. \n");
    unsigned long new_address = 4;
    set_modbus_address(fd, new_address);
    LOG_DEBUG("Done! \n");

    LOG_DEBUG(">> Writing 4 registers.. \n");
    memset(buf, 0, n_buf_len * sizeof(uint16_t));  // reset previous read
    n_regs = 4;
    buf[0] = 0xF0C5;
    buf[1] = 0xF0C6;
    buf[2] = 0xF0C7;
    buf[3] = 0xF0C8;
    write_to_modbus(fd, buf, n_regs);
    LOG_DEBUG("Done! \n");

    LOG_DEBUG(">> Reading 16 registers.. \n");
    n_regs = 16;
    read_from_modbus(fd, buf, n_regs);
    LOG_DEBUG("Done! \n");

    LOG_DEBUG(">> Setting wrong address... \n");
    new_address = UINT16_MAX + 1;
    set_modbus_address(fd, new_address);
    LOG_DEBUG("Done! \n");

    LOG_DEBUG(">> Reading too much... \n");
    n_regs = 65;
    read_from_modbus(fd, buf, n_regs);
    LOG_DEBUG("Done! \n");

    cleanup();

    return EXIT_SUCCESS;
}
