#ifndef BYTE_FIFO_H_
#define BYTE_FIFO_H_

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include < stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

struct byte_fifo_t
{
    unsigned char* const data;
    const unsigned int size;
    unsigned int write_index;
    unsigned int read_index;
    unsigned int n_elements;
};

int16_t byte_fifo_init(struct byte_fifo_t* const fifo);
int16_t byte_fifo_is_available(struct byte_fifo_t* const fifo);
int16_t byte_fifo_write(struct byte_fifo_t* const fifo, const unsigned char* const bytes, unsigned int len);
int16_t byte_fifo_read(struct byte_fifo_t* const fifo, unsigned char* const buffer, unsigned int max_len);
int16_t byte_fifo_reset(struct byte_fifo_t* const fifo);

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif  // BYTE_FIFO_H_