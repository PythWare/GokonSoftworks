#ifndef RESTABLE_H
#define RESTABLE_H
#include "util.h"
#define RESTABLE_MAGIC 0x00033B59u

typedef struct {
    uint32_t count;
    uint32_t unit;
    size_t table_end;
    size_t *offs;
    size_t *sizes;
} restable_layout;

int restable_read(const unsigned char *data, size_t len, arena *a, restable_layout *out);
int restable_build(const unsigned char *original, const restable_layout *layout,
                   const unsigned char *const *chunks, const size_t *lens, size_t count, buf *out, err *e);

#endif
