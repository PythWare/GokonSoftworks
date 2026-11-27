#ifndef LZP2_H
#define LZP2_H
#include "util.h"
#define LZP2_HEADER 16

int lzp2_looks_like(const unsigned char *data, size_t len);
uint32_t lzp2_original_size(const unsigned char *data, size_t len);
float lzp2_version(const unsigned char *data, size_t len);

#define LZP2_CHUNK 0x80000
#define LZP2_ALIGN 2048

int lzp2_decompress(const unsigned char *data, size_t len, buf *out, err *e);
int lzp2_decompress_prefix(const unsigned char *data, size_t len, buf *out,
                           size_t want, err *e);
uint32_t lzp2_chain_original(const unsigned char *data, size_t len);
int lzp2_chain_streams(const unsigned char *data, size_t len);
int lzp2_decompress_chain(const unsigned char *data, size_t len, buf *out, err *e);
uint32_t lzp2_chain_chunk(const unsigned char *data, size_t len);
int lzp2_compress_chain(const unsigned char *data, size_t len, float version,
                        size_t chunk, buf *out, err *e);
int lzp2_compress(const unsigned char *data, size_t len, float version,
                  buf *out, err *e);

#endif
