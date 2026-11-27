#ifndef SEBANK_H
#define SEBANK_H
#include "util.h"

typedef struct {
    uint32_t count;
    size_t *offs;
    size_t *sizes;
} sebank_layout;

int sebank_read(const unsigned char *data, size_t len, arena *a, sebank_layout *out);
void sebank_child_name(uint32_t index, char *name, size_t cap);
int sebank_build(const unsigned char *const *chunks, const size_t *lens, size_t count, buf *out, err *e);
int sebank_index(const size_t *lens, size_t count, int terminated, buf *out, err *e);

#endif
