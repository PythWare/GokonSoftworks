#ifndef OFFTABLE_H
#define OFFTABLE_H
#include "util.h"

typedef struct {
    uint32_t count;
    size_t table_end;
    size_t *offs;
    size_t *sizes;
} offtable_layout;

int offtable_read(const unsigned char *data, size_t len, arena *a, offtable_layout *out);
int offtable_build(const unsigned char *const *chunks, const size_t *lens, size_t count, buf *out, err *e);

#endif
