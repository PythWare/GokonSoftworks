#ifndef G1TSTACK_H
#define G1TSTACK_H
#include "util.h"

typedef struct {
    size_t stride;
    uint32_t count;
} g1tstack_layout;

int g1tstack_read(const unsigned char *data, size_t len, g1tstack_layout *out);
size_t g1tstack_slot_size(const unsigned char *data, const g1tstack_layout *layout, uint32_t index);
int g1tstack_build(const g1tstack_layout *layout, const unsigned char *const *chunks, const size_t *lens, size_t count,
                   buf *out, err *e);

#endif
