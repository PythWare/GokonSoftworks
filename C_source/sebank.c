#include "sebank.h"
#include "codec.h"
#include <stdio.h>
#include <string.h>

#define SEBANK_ALIGN 16
#define SEBANK_HEADER 16
#define SEBANK_MAX_COUNT 200000

static size_t align_up(size_t value) {
    return (value + SEBANK_ALIGN - 1) / SEBANK_ALIGN * SEBANK_ALIGN;
}

static size_t sebank_walk(const unsigned char *data, size_t len, size_t *offs, size_t *sizes) {
    size_t pos = 0;
    size_t count = 0;
    while (pos < len) {
        const char *magic = count % 2 == 0 ? "_HBW0000" : "_DBW0000";
        if (pos + SEBANK_HEADER > len || memcmp(data + pos, magic, 8) != 0 || count >= SEBANK_MAX_COUNT) {
            return 0;
        }
        size_t size = codec_u32(data, pos + 8);
        if (size < SEBANK_HEADER || size > len - pos) {
            return 0;
        }
        size_t end = pos + size;
        size_t next = align_up(end);
        if (next > len) {
            return 0;
        }
        for (size_t k = end; k < next; k++) {
            if (data[k] != 0) {
                return 0;
            }
        }
        if (offs != NULL) {
            offs[count] = pos;
            sizes[count] = size;
        }
        count++;
        pos = next;
    }
    return count >= 2 && count % 2 == 0 ? count : 0;
}

int sebank_read(const unsigned char *data, size_t len, arena *a, sebank_layout *out) {
    if (len < SEBANK_HEADER * 2 || memcmp(data, "_HBW0000", 8) != 0) {
        return 0;
    }
    size_t count = sebank_walk(data, len, NULL, NULL);
    if (count == 0) {
        return 0;
    }
    size_t *offs = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    size_t *sizes = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    if (offs == NULL || sizes == NULL) {
        return 0;
    }
    sebank_walk(data, len, offs, sizes);
    out->count = (uint32_t)count;
    out->offs = offs;
    out->sizes = sizes;
    return 1;
}

void sebank_child_name(uint32_t index, char *name, size_t cap) {
    snprintf(name, cap, "%03u%s", index / 2, index % 2 == 0 ? ".wbh" : ".wbd");
}

int sebank_build(const unsigned char *const *chunks, const size_t *lens, size_t count, buf *out, err *e) {
    static const unsigned char zeros[SEBANK_ALIGN];
    buf_reset(out);
    for (size_t i = 0; i < count; i++) {
        size_t pad = align_up(lens[i]) - lens[i];
        if ((lens[i] > 0 && !buf_put(out, chunks[i], lens[i])) || (pad > 0 && !buf_put(out, zeros, pad))) {
            err_set(e, "out of memory rebuilding a sound bank file");
            return 0;
        }
        if (out->len > 0xFFFFFFFFu) {
            err_set(e, "the sound bank file grew past 4 GB, which its index cant address");
            return 0;
        }
    }
    return 1;
}

static int put_be32(buf *out, size_t value) {
    unsigned char word[4];
    word[0] = (unsigned char)((value >> 24) & 0xFF);
    word[1] = (unsigned char)((value >> 16) & 0xFF);
    word[2] = (unsigned char)((value >> 8) & 0xFF);
    word[3] = (unsigned char)(value & 0xFF);
    return buf_put(out, word, 4);
}

int sebank_index(const size_t *lens, size_t count, int terminated, buf *out, err *e) {
    buf_reset(out);
    int ok = put_be32(out, count) && put_be32(out, 0);
    size_t pos = 0;
    for (size_t i = 0; ok && i < count; i++) {
        if (pos > 0xFFFFFFFFu || lens[i] > 0xFFFFFFFFu) {
            err_set(e, "stream %zu sits past 4 GB, which the index cant address", i);
            return 0;
        }
        ok = put_be32(out, pos) && put_be32(out, lens[i]);
        pos = align_up(pos + lens[i]);
    }
    if (ok && terminated) {
        ok = put_be32(out, 0) && put_be32(out, 0);
    }
    if (!ok) {
        err_set(e, "out of memory building a stream index");
    }
    return ok;
}
