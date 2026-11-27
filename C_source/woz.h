#ifndef WOZ_H
#define WOZ_H
#include "util.h"
#define WOZ_LZP2_VERSION 1.02f

int woz_pack_entry(const unsigned char *original, size_t original_len, buf *chunk, err *e);
int woz_chunk_list(const unsigned char *data, size_t len, buf *table, err *e);

#endif
