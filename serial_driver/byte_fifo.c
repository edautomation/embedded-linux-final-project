#include "byte_fifo.h"

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>  // size_t
#include <stdint.h>  // uintx_t
#endif

#define RETURN_IF(x, y) \
    if ((x)) return (y)

int byte_fifo_init(struct byte_fifo_t* const fifo)
{
    RETURN_IF(NULL == fifo, EFAULT);
    RETURN_IF(NULL == fifo->data, EFAULT);
    RETURN_IF(0 == fifo->size, EFAULT);

    fifo->write_index = 0U;
    fifo->read_index = 0U;
    fifo->n_elements = 0U;

    memset((void*)fifo->data, 0, fifo->size);

    mutex_init(&fifo->lock);

    return 0;
}

int byte_fifo_is_available(struct byte_fifo_t* const fifo)
{
    RETURN_IF(NULL == fifo, EFAULT);

    mutex_lock(&fifo->lock);
    int is_available = (fifo->n_elements < fifo->size);
    mutex_unlock(&fifo->lock);

    return is_available;
}

int byte_fifo_write(struct byte_fifo_t* const fifo, const unsigned char* const bytes, unsigned int len)
{
    RETURN_IF(NULL == fifo, EFAULT);
    RETURN_IF(NULL == fifo->data, EFAULT);
    RETURN_IF(NULL == bytes, EFAULT);

    int n_bytes_written = 0;
    int n_bytes_overwritten = 0;
    while (len > 0)
    {
        mutex_lock(&fifo->lock);
        if (fifo->n_elements < fifo->size)
        {
            unsigned int write_index = fifo->write_index;
            fifo->data[write_index] = bytes[n_bytes_written];
            fifo->write_index = (write_index < (fifo->size - 1)) ? write_index + 1 : 0;
            fifo->n_elements++;

            // Update
            n_bytes_written++;
            len--;
        }
        else
        {
            unsigned int read_index = fifo->read_index;
            fifo->read_index = (read_index < (fifo->size - 1)) ? read_index + 1 : 0;
            fifo->n_elements--;
            n_bytes_overwritten++;
        }

        mutex_unlock(&fifo->lock);
    }

    return n_bytes_overwritten;
}

int byte_fifo_read(struct byte_fifo_t* const fifo, unsigned char* const buffer, unsigned int max_len)
{
    RETURN_IF(NULL == fifo, EFAULT);
    RETURN_IF(NULL == fifo->data, EFAULT);
    RETURN_IF(NULL == buffer, EFAULT);

    int n_bytes_read = 0;
    while (n_bytes_read < max_len)
    {
        mutex_lock(&fifo->lock);
        if (fifo->n_elements > 0)
        {
            unsigned int read_index = fifo->read_index;
            buffer[n_bytes_read] = fifo->data[read_index];
            fifo->read_index = (read_index < (fifo->size - 1)) ? read_index + 1 : 0;
            fifo->n_elements--;
            n_bytes_read++;
            mutex_unlock(&fifo->lock);
        }
        else
        {
            mutex_unlock(&fifo->lock);
            break;
        }
    }

    return n_bytes_read;
}