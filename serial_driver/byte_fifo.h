#ifndef BYTE_FIFO_H_
#define BYTE_FIFO_H_

// !! NOT THREAD-SAFE !!

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

struct byte_fifo_t
{
    char* const data;
    const unsigned int size;
    unsigned int write_index;
    unsigned int read_index;
    unsigned int n_elements;
};

int byte_fifo_init(struct byte_fifo_t* const fifo);
int byte_fifo_is_available(const struct byte_fifo_t* const fifo);
int byte_fifo_write(struct byte_fifo_t* const fifo, const char* const bytes, unsigned int len);
int byte_fifo_read(struct byte_fifo_t* const fifo, char* const buffer, unsigned int max_len);

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif  // BYTE_FIFO_H_