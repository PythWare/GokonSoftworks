#ifndef BTSC_H
#define BTSC_H
#include "util.h"
#define BTSC_DATA_START 0x800
#define BTSC_ALIGN 0x800
#define BTSC_BLOCK 0x400000

int btsc_looks_like(const unsigned char *data, size_t len);
uint32_t btsc_block_size(const unsigned char *data, size_t len);
int btsc_decompress(const unsigned char *data, size_t len, buf *out, err *e);
int btsc_compress(const unsigned char *data, size_t len, uint32_t block, buf *out, err *e);

#endif
