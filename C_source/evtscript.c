#include "evtscript.h"
#include "codec.h"
#include <stdlib.h>
#include <string.h>

#define EVT_MAX_SECTIONS 4096
#define EVT_ALIGN 16
#define EVT_MAX_TYPE 0x40
#define EVT_END_OP 0xFF
#define EVT_MASK_SIZE 4

static size_t arg_size(unsigned type) {
    switch (type) {
    case 0:
        return 1;
    case 2:
    case 3:
    case 5:
    case 6:
    case 7:
    case 8:
        return 4;
    default:
        return 2;
    }
}

static int chain_walk(const unsigned char *d, size_t len, size_t *ptr_pos, size_t ptr_cap, size_t *ptr_count,
                      size_t *args_end) {
    size_t pos = 0;
    size_t found = 0;
    for (;;) {
        if (pos + 8 > len) {
            return 0;
        }
        size_t next = codec_u32(d, pos);
        unsigned op = d[pos + 4];
        size_t n = d[pos + 6];
        if (pos + 8 + 2 * n > len) {
            return 0;
        }
        if (op != EVT_END_OP && (next <= pos || next + 8 > len)) {
            return 0;
        }
        size_t limit = op == EVT_END_OP ? len : next;
        const unsigned char *types = d + pos + 7;
        const unsigned char *adv = d + pos + 8 + n;
        size_t at = pos + 7 + d[pos + 7 + n];
        size_t last = pos + 8 + 2 * n;
        for (size_t i = 0; i < n; i++) {
            unsigned type = types[i];
            size_t size = arg_size(type);
            if (type >= EVT_MAX_TYPE || at + size > limit) {
                return 0;
            }
            if (type == 7 || type == 8) {
                if (ptr_pos != NULL && found < ptr_cap) {
                    ptr_pos[found] = at;
                }
                found++;
            }
            if (at + size > last) {
                last = at + size;
            }
            at += adv[i];
        }
        if (op == EVT_END_OP) {
            *ptr_count = found;
            *args_end = last;
            return 1;
        }
        pos = next;
    }
}

static int compare_size(const void *x, const void *y) {
    size_t a = *(const size_t *)x;
    size_t b = *(const size_t *)y;
    return a < b ? -1 : a > b ? 1 : 0;
}

static size_t place_like(size_t cursor, size_t original_off) {
    size_t at = cursor - cursor % EVT_ALIGN + original_off % EVT_ALIGN;
    return at >= cursor ? at : at + EVT_ALIGN;
}

static int put_zeros(buf *out, size_t n) {
    static const unsigned char zeros[64];
    while (n > 0) {
        size_t step = n < sizeof(zeros) ? n : sizeof(zeros);
        if (!buf_put(out, zeros, step)) {
            return 0;
        }
        n -= step;
    }
    return 1;
}

static void patch_u32(buf *out, size_t at, size_t value) {
    unsigned char *p = (unsigned char *)out->data + at;
    p[0] = (unsigned char)(value & 0xFF);
    p[1] = (unsigned char)((value >> 8) & 0xFF);
    p[2] = (unsigned char)((value >> 16) & 0xFF);
    p[3] = (unsigned char)((value >> 24) & 0xFF);
}

int evt_section_read(const unsigned char *data, size_t len, arena *a, evt_section_layout *out) {
    size_t count = 0;
    size_t args_end = 0;
    if (len < 16 || !chain_walk(data, len, NULL, 0, &count, &args_end) || count == 0) {
        return 0;
    }
    size_t *pos = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    size_t *targets = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    size_t *sizes = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    if (pos == NULL || targets == NULL || sizes == NULL) {
        return 0;
    }
    chain_walk(data, len, pos, count, &count, &args_end);
    for (size_t i = 0; i < count; i++) {
        targets[i] = codec_u32(data, pos[i]);
    }
    qsort(targets, count, sizeof(size_t), compare_size);
    size_t unique = 0;
    for (size_t i = 0; i < count; i++) {
        if (unique == 0 || targets[i] != targets[unique - 1]) {
            targets[unique++] = targets[i];
        }
    }
    for (size_t j = 0; j < unique; j++) {
        size_t at = targets[j];
        if (at < args_end || at + EVT_MASK_SIZE > len || memcmp(data + at, "\xFF\xFF\xFF\xFF", EVT_MASK_SIZE) != 0) {
            return 0;
        }
        if (j > 0 && at - targets[j - 1] < EVT_MASK_SIZE) {
            return 0;
        }
    }
    for (size_t j = 0; j < unique; j++) {
        sizes[j] = (j + 1 < unique ? targets[j + 1] : len) - targets[j];
    }
    out->head = targets[0];
    out->count = (uint32_t)unique;
    out->offs = targets;
    out->sizes = sizes;
    return 1;
}

int evt_read(const unsigned char *data, size_t len, arena *a, evt_layout *out) {
    if (len < 32) {
        return 0;
    }
    uint32_t count = codec_u32(data, 0);
    if (count == 0 || count > EVT_MAX_SECTIONS) {
        return 0;
    }
    size_t table_end = 4 + (size_t)count * 4;
    size_t first = (table_end + EVT_ALIGN - 1) / EVT_ALIGN * EVT_ALIGN;
    if (first >= len || codec_u32(data, 4) != first) {
        return 0;
    }
    for (size_t k = table_end; k < first; k++) {
        if (data[k] != 0) {
            return 0;
        }
    }
    size_t *offs = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    size_t *sizes = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    if (offs == NULL || sizes == NULL) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        size_t off = codec_u32(data, 4 + (size_t)i * 4);
        if (off % EVT_ALIGN != 0 || off >= len || (i > 0 && off <= offs[i - 1])) {
            return 0;
        }
        offs[i] = off;
    }
    for (uint32_t i = 0; i < count; i++) {
        sizes[i] = (i + 1 < count ? offs[i + 1] : len) - offs[i];
        size_t pointers = 0;
        size_t args_end = 0;
        if (!chain_walk(data + offs[i], sizes[i], NULL, 0, &pointers, &args_end)) {
            return 0;
        }
    }
    out->count = count;
    out->offs = offs;
    out->sizes = sizes;
    return 1;
}

int evt_build(const unsigned char *original, const evt_layout *layout, const unsigned char *const *chunks,
              const size_t *lens, size_t count, buf *out, err *e) {
    if (count != layout->count) {
        err_set(e, "event folder has %zu sections but the original holds %u", count, layout->count);
        return 0;
    }
    buf_reset(out);
    if (!buf_put(out, original, layout->offs[0])) {
        err_set(e, "out of memory rebuilding an event file");
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        size_t at = place_like(out->len, layout->offs[i]);
        if (at > 0xFFFFFFFFu) {
            err_set(e, "event section %zu starts past 4 GB", i);
            return 0;
        }
        if (!put_zeros(out, at - out->len) || (lens[i] > 0 && !buf_put(out, chunks[i], lens[i]))) {
            err_set(e, "out of memory rebuilding an event file");
            return 0;
        }
        patch_u32(out, 4 + i * 4, at);
    }
    return 1;
}

static size_t find_block(const evt_section_layout *layout, size_t value) {
    size_t lo = 0;
    size_t hi = layout->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (layout->offs[mid] < value) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < layout->count && layout->offs[lo] == value ? lo : (size_t)-1;
}

int evt_section_build(const unsigned char *original, const evt_section_layout *layout,
                      const unsigned char *const *chunks, const size_t *lens, size_t count, buf *out, err *e) {
    if (count != (size_t)layout->count + 1) {
        err_set(e, "event script folder has %zu files but the original holds a head and %u blocks", count,
                layout->count);
        return 0;
    }
    const unsigned char *head = chunks[0];
    size_t head_len = lens[0];
    size_t pointers = 0;
    size_t args_end = 0;
    if (!chain_walk(head, head_len, NULL, 0, &pointers, &args_end)) {
        err_set(e, "the event script head (000.bin) no longer parses as a command chain");
        return 0;
    }
    size_t *pos = (size_t *)malloc(sizeof(size_t) * (pointers > 0 ? pointers : 1));
    size_t *placed = (size_t *)malloc(sizeof(size_t) * (layout->count > 0 ? layout->count : 1));
    if (pos == NULL || placed == NULL) {
        free(pos);
        free(placed);
        err_set(e, "out of memory rebuilding an event script");
        return 0;
    }
    chain_walk(head, head_len, pos, pointers, &pointers, &args_end);
    int ok = 1;
    buf_reset(out);
    if (!buf_put(out, head, head_len)) {
        ok = 0;
        err_set(e, "out of memory rebuilding an event script");
    }
    for (size_t j = 0; ok && j < layout->count; j++) {
        size_t at = place_like(out->len, layout->offs[j]);
        if (at > 0xFFFFFFFFu) {
            err_set(e, "event script block %zu starts past 4 GB", j);
            ok = 0;
            break;
        }
        placed[j] = at;
        if (!put_zeros(out, at - out->len) || !buf_put(out, original + layout->offs[j], EVT_MASK_SIZE) ||
            (lens[j + 1] > 0 && !buf_put(out, chunks[j + 1], lens[j + 1]))) {
            err_set(e, "out of memory rebuilding an event script");
            ok = 0;
        }
    }
    for (size_t i = 0; ok && i < pointers; i++) {
        size_t value = codec_u32(head, pos[i]);
        size_t block = find_block(layout, value);
        if (block == (size_t)-1) {
            err_set(e, "event script pointer 0x%zx at 0x%zx doesnt match a block of the original", value, pos[i]);
            ok = 0;
            break;
        }
        patch_u32(out, pos[i], placed[block]);
    }
    free(pos);
    free(placed);
    return ok;
}
