#include "restable.h"
#include "codec.h"
#include <string.h>

static size_t align_to(size_t value, size_t unit) {
    return (value + unit - 1) / unit * unit;
}

int restable_read(const unsigned char *data, size_t len, arena *a, restable_layout *out) {
    if (len < 24 || codec_u32(data, 0) != RESTABLE_MAGIC) {
        return 0;
    }
    uint32_t count = codec_u32(data, 4);
    uint32_t unit = codec_u32(data, 8);
    if (count == 0 || count > 100000 || unit < 4 || unit > 0x10000 || (unit & (unit - 1)) != 0) {
        return 0;
    }
    size_t table_end = 16 + (size_t)count * 8;
    if (table_end > len) {
        return 0;
    }
    size_t *offs = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    size_t *sizes = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    if (offs == NULL || sizes == NULL) {
        return 0;
    }
    size_t pos = align_to(table_end, unit);
    for (size_t i = table_end; i < pos && i < len; i++) {
        if (data[i] != 0) {
            return 0;
        }
    }
    for (uint32_t i = 0; i < count; i++) {
        size_t off = (size_t)codec_u32(data, 16 + (size_t)i * 8) * unit;
        size_t size = codec_u32(data, 16 + (size_t)i * 8 + 4);
        if (off != pos || off + size > len) {
            return 0;
        }
        size_t next = align_to(off + size, unit);
        if (next > len) {
            return 0;
        }
        for (size_t k = off + size; k < next; k++) {
            if (data[k] != 0) {
                return 0;
            }
        }
        offs[i] = off;
        sizes[i] = size;
        pos = next;
    }
    if (pos != len) {
        return 0;
    }
    out->count = count;
    out->unit = unit;
    out->table_end = table_end;
    out->offs = offs;
    out->sizes = sizes;
    return 1;
}

static void put_u32(unsigned char *at, uint32_t v) {
    at[0] = (unsigned char)(v & 0xFF);
    at[1] = (unsigned char)((v >> 8) & 0xFF);
    at[2] = (unsigned char)((v >> 16) & 0xFF);
    at[3] = (unsigned char)((v >> 24) & 0xFF);
}

int restable_build(const unsigned char *original, const restable_layout *layout,
                   const unsigned char *const *chunks, const size_t *lens, size_t count, buf *out, err *e) {
    if (count != layout->count) {
        err_set(e, "resource table folder has %zu files but the table holds %u", count, layout->count);
        return 0;
    }
    size_t unit = layout->unit;
    size_t start = align_to(layout->table_end, unit);
    buf_reset(out);
    if (!buf_put(out, original, 16)) {
        err_set(e, "out of memory rebuilding a resource table");
        return 0;
    }
    while (out->len < start) {
        if (!buf_putc(out, 0)) {
            err_set(e, "out of memory rebuilding a resource table");
            return 0;
        }
    }
    for (size_t i = 0; i < count; i++) {
        size_t off = out->len;
        if (off / unit > 0xFFFFFFFFu || lens[i] > 0xFFFFFFFFu) {
            err_set(e, "resource table entry %zu is too large for its row", i);
            return 0;
        }
        put_u32((unsigned char *)out->data + 16 + i * 8, (uint32_t)(off / unit));
        put_u32((unsigned char *)out->data + 16 + i * 8 + 4, (uint32_t)lens[i]);
        if ((lens[i] > 0 && !buf_put(out, chunks[i], lens[i]))) {
            err_set(e, "out of memory rebuilding a resource table");
            return 0;
        }
        while (out->len % unit != 0) {
            if (!buf_putc(out, 0)) {
                err_set(e, "out of memory rebuilding a resource table");
                return 0;
            }
        }
    }
    return 1;
}
