#include "offtable.h"
#include "codec.h"
#include "nested.h"
#include <string.h>

#define OFFTABLE_MAX_COUNT 100000
#define OFFTABLE_MAX_DEPTH 6

static int offtable_shape(const unsigned char *data, size_t len, uint32_t *count_out) {
    if (len < 8) {
        return 0;
    }
    uint32_t count = codec_u32(data, 0);
    if (count == 0 || count > OFFTABLE_MAX_COUNT) {
        return 0;
    }
    size_t table_end = 4 + (size_t)count * 4;
    if (table_end > len || codec_u32(data, 4) != table_end) {
        return 0;
    }
    size_t previous = table_end;
    for (uint32_t i = 1; i < count; i++) {
        size_t off = codec_u32(data, 4 + (size_t)i * 4);
        if (off < previous || off > len) {
            return 0;
        }
        previous = off;
    }
    *count_out = count;
    return 1;
}

static size_t entry_end(const unsigned char *data, size_t len, uint32_t count, uint32_t i) {
    return i + 1 < count ? codec_u32(data, 4 + ((size_t)i + 1) * 4) : len;
}

static int offtable_evidence(const unsigned char *data, size_t len, int depth) {
    uint32_t count = 0;
    if (depth > OFFTABLE_MAX_DEPTH || !offtable_shape(data, len, &count)) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        size_t off = codec_u32(data, 4 + (size_t)i * 4);
        size_t end = entry_end(data, len, count, i);
        if (end <= off) {
            continue;
        }
        const unsigned char *child = data + off;
        size_t size = end - off;
        if (nested_match_known_signature(child, size, 0) != NULL || offtable_evidence(child, size, depth + 1)) {
            return 1;
        }
    }
    return 0;
}

int offtable_read(const unsigned char *data, size_t len, arena *a, offtable_layout *out) {
    uint32_t count = 0;
    if (!offtable_shape(data, len, &count) || !offtable_evidence(data, len, 0)) {
        return 0;
    }
    size_t *offs = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    size_t *sizes = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    if (offs == NULL || sizes == NULL) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        offs[i] = codec_u32(data, 4 + (size_t)i * 4);
        sizes[i] = entry_end(data, len, count, i) - offs[i];
    }
    out->count = count;
    out->table_end = 4 + (size_t)count * 4;
    out->offs = offs;
    out->sizes = sizes;
    return 1;
}

int offtable_build(const unsigned char *const *chunks, const size_t *lens, size_t count, buf *out, err *e) {
    if (count == 0 || count > OFFTABLE_MAX_COUNT) {
        err_set(e, "an offset table needs between 1 and %d entries, the folder has %zu", OFFTABLE_MAX_COUNT, count);
        return 0;
    }
    buf_reset(out);
    size_t pos = 4 + count * 4;
    unsigned char word[4];
    word[0] = (unsigned char)(count & 0xFF);
    word[1] = (unsigned char)((count >> 8) & 0xFF);
    word[2] = (unsigned char)((count >> 16) & 0xFF);
    word[3] = (unsigned char)((count >> 24) & 0xFF);
    if (!buf_put(out, word, 4)) {
        err_set(e, "out of memory rebuilding an offset table");
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        if (pos > 0xFFFFFFFFu) {
            err_set(e, "offset table entry %zu starts past 4 GB", i);
            return 0;
        }
        word[0] = (unsigned char)(pos & 0xFF);
        word[1] = (unsigned char)((pos >> 8) & 0xFF);
        word[2] = (unsigned char)((pos >> 16) & 0xFF);
        word[3] = (unsigned char)((pos >> 24) & 0xFF);
        if (!buf_put(out, word, 4)) {
            err_set(e, "out of memory rebuilding an offset table");
            return 0;
        }
        pos += lens[i];
    }
    for (size_t i = 0; i < count; i++) {
        if (lens[i] > 0 && !buf_put(out, chunks[i], lens[i])) {
            err_set(e, "out of memory rebuilding an offset table");
            return 0;
        }
    }
    return 1;
}
