#include "g1tstack.h"
#include "codec.h"
#include <string.h>

#define G1TSTACK_ALIGN 0x800
#define G1TSTACK_HEADER 0x1C
#define G1TSTACK_MAX_COUNT 100000

int g1tstack_read(const unsigned char *data, size_t len, g1tstack_layout *out) {
    if (len < G1TSTACK_ALIGN * 2 || memcmp(data, "GT1G", 4) != 0) {
        return 0;
    }
    size_t first = codec_u32(data, 8);
    if (first < G1TSTACK_HEADER) {
        return 0;
    }
    size_t stride = (first + G1TSTACK_ALIGN - 1) / G1TSTACK_ALIGN * G1TSTACK_ALIGN;
    if (len % stride != 0 || len / stride < 2 || len / stride > G1TSTACK_MAX_COUNT) {
        return 0;
    }
    size_t count = len / stride;
    for (size_t i = 0; i < count; i++) {
        const unsigned char *slot = data + i * stride;
        if (memcmp(slot, "GT1G", 4) != 0) {
            return 0;
        }
        size_t size = codec_u32(slot, 8);
        if (size < G1TSTACK_HEADER || size > stride) {
            return 0;
        }
        for (size_t k = size; k < stride; k++) {
            if (slot[k] != 0) {
                return 0;
            }
        }
    }
    out->stride = stride;
    out->count = (uint32_t)count;
    return 1;
}

size_t g1tstack_slot_size(const unsigned char *data, const g1tstack_layout *layout, uint32_t index) {
    return codec_u32(data + (size_t)index * layout->stride, 8);
}

int g1tstack_build(const g1tstack_layout *layout, const unsigned char *const *chunks, const size_t *lens, size_t count,
                   buf *out, err *e) {
    if (count != layout->count) {
        err_set(e, "texture stack folder has %zu files but the game reads %u fixed slots", count, layout->count);
        return 0;
    }
    static const unsigned char zeros[G1TSTACK_ALIGN];
    buf_reset(out);
    for (size_t i = 0; i < count; i++) {
        if (lens[i] > layout->stride) {
            err_set(e, "texture %03zu is %zu bytes but the game reads this file in fixed 0x%zx byte slots, so it "
                       "has to stay at or under %zu bytes",
                    i, lens[i], layout->stride, layout->stride);
            return 0;
        }
        if (lens[i] > 0 && !buf_put(out, chunks[i], lens[i])) {
            err_set(e, "out of memory rebuilding a texture stack");
            return 0;
        }
        size_t pad = layout->stride - lens[i];
        while (pad > 0) {
            size_t step = pad < sizeof(zeros) ? pad : sizeof(zeros);
            if (!buf_put(out, zeros, step)) {
                err_set(e, "out of memory rebuilding a texture stack");
                return 0;
            }
            pad -= step;
        }
    }
    return 1;
}
