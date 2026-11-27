#include "zp1.h"
#include "codec.h"
#include <stdlib.h>
#include <string.h>
#define ZP1_MAX_CHUNKS 8192

static void put_u32(unsigned char *at, uint32_t v) {
    at[0] = (unsigned char)(v & 0xFF);
    at[1] = (unsigned char)((v >> 8) & 0xFF);
    at[2] = (unsigned char)((v >> 16) & 0xFF);
    at[3] = (unsigned char)((v >> 24) & 0xFF);
}

static size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

int zp1_looks_like(const unsigned char *data, size_t len) {
    if (len < 20 || memcmp(data, "zp1", 3) != 0) {
        return 0;
    }
    uint32_t count = codec_u32(data, 12);
    if (count == 0 || count > ZP1_MAX_CHUNKS) {
        return 0;
    }
    if (16 + (size_t)count * 4 > len || len < ZP1_PAYLOAD) {
        return 0;
    }
    uint32_t chunk = codec_u32(data, 8);
    if (chunk == 0) {
        return 0;
    }
    uint64_t original = codec_u32(data, 4);
    return original <= (uint64_t)chunk * count;
}

uint32_t zp1_original_size(const unsigned char *data, size_t len) {
    return len >= 8 ? codec_u32(data, 4) : 0;
}

uint32_t zp1_chunk_size(const unsigned char *data, size_t len) {
    return len >= 12 ? codec_u32(data, 8) : 0;
}

uint32_t zp1_chunk_count(const unsigned char *data, size_t len) {
    return len >= 16 ? codec_u32(data, 12) : 0;
}

unsigned char zp1_version(const unsigned char *data, size_t len) {
    return len >= 4 ? data[3] : 0;
}

int zp1_decompress(const unsigned char *data, size_t len, buf *out, err *e) {
    if (!zp1_looks_like(data, len)) {
        err_set(e, "not a zp1 stream");
        return 0;
    }
    uint32_t original = codec_u32(data, 4);
    uint32_t count = codec_u32(data, 12);

    buf_reset(out);
    if (!buf_reserve(out, original)) {
        err_set(e, "out of memory for %u decompressed bytes", original);
        return 0;
    }

    buf piece;
    buf_init(&piece);
    size_t at = ZP1_PAYLOAD;
    for (uint32_t i = 0; i < count; i++) {
        if (at + 4 > len) {
            buf_free(&piece);
            err_set(e, "zp1 chunk %u starts past the end of the entry", i);
            return 0;
        }
        uint32_t declared = codec_u32(data, 16 + (size_t)i * 4);
        uint32_t packed = codec_u32(data, at);
        if (packed == 0) {
            at = align_up(at + (declared > 4 ? declared : 4), ZP1_ALIGN);
            continue;
        }

        err quiet;
        err_clear(&quiet);
        int deflated = at + 4 + (size_t)packed <= len &&
                       codec_inflate(data + at + 4, packed, &piece, &quiet);
        if (deflated) {
            if (piece.len > 0 && !buf_put(out, piece.data, piece.len)) {
                buf_free(&piece);
                err_set(e, "out of memory joining a zp1 stream");
                return 0;
            }
        } else {
            if (declared == 0 || at + (size_t)declared > len) {
                buf_free(&piece);
                err_set(e, "zp1 chunk %u claims %u bytes with %zu left", i, packed,
                        len - at);
                return 0;
            }
            if (!buf_put(out, data + at, declared)) {
                buf_free(&piece);
                err_set(e, "out of memory copying a stored zp1 chunk");
                return 0;
            }
        }
        at = align_up(at + (declared > 4 ? declared : 4 + packed), ZP1_ALIGN);
    }
    buf_free(&piece);

    if (out->len == 0 && original > 0) {
        for (uint32_t i = 0; i < original; i++) {
            if (!buf_putc(out, 0)) {
                err_set(e, "out of memory expanding an empty zp1 entry");
                return 0;
            }
        }
    }
    if (out->len < original) {
        err_set(e, "zp1 stream ended early: %zu of %u bytes", out->len, original);
        return 0;
    }
    out->len = original;
    return 1;
}

int zp1_compress(const unsigned char *data, size_t len, uint32_t chunk,
                 unsigned char version, buf *out, err *e) {
    if (chunk == 0) {
        chunk = ZP1_CHUNK;
    }
    size_t count = len == 0 ? 1 : (len + chunk - 1) / chunk;
    if (count > ZP1_MAX_CHUNKS) {
        err_set(e, "%zu bytes needs %zu zp1 chunks, more than the format holds", len, count);
        return 0;
    }

    buf_reset(out);
    unsigned char head[16];
    memcpy(head, "zp1", 3);
    head[3] = version;
    put_u32(head + 4, (uint32_t)len);
    put_u32(head + 8, chunk);
    put_u32(head + 12, (uint32_t)count);
    if (!buf_put(out, head, sizeof(head))) {
        err_set(e, "out of memory starting a zp1 stream");
        return 0;
    }
    for (size_t i = 0; i < count * 4; i++) {
        if (!buf_putc(out, 0)) {
            err_set(e, "out of memory reserving the zp1 chunk table");
            return 0;
        }
    }
    while (out->len < ZP1_PAYLOAD) {
        if (!buf_putc(out, 0)) {
            err_set(e, "out of memory padding a zp1 header");
            return 0;
        }
    }

    uint32_t *sizes = (uint32_t *)calloc(count, sizeof(uint32_t));
    if (sizes == NULL) {
        err_set(e, "out of memory sizing %zu zp1 chunks", count);
        return 0;
    }

    buf piece;
    buf_init(&piece);
    int ok = 1;
    for (size_t i = 0; i < count && ok; i++) {
        size_t start = i * chunk;
        size_t take = len - start < chunk ? len - start : chunk;
        if (!codec_deflate(data + start, take, 9, &piece, e)) {
            ok = 0;
            break;
        }
        unsigned char prefix[4];
        put_u32(prefix, (uint32_t)piece.len);
        size_t before = out->len;
        ok = buf_put(out, prefix, 4) && buf_put(out, piece.data, piece.len);
        if (!ok) {
            err_set(e, "out of memory writing a zp1 chunk");
            break;
        }
        sizes[i] = (uint32_t)(out->len - before);
        size_t padded = align_up(out->len, ZP1_ALIGN);
        while (out->len < padded && ok) {
            ok = buf_putc(out, 0);
        }
        if (!ok) {
            err_set(e, "out of memory padding a zp1 chunk");
            break;
        }
    }
    buf_free(&piece);

    if (ok) {
        unsigned char *table = (unsigned char *)out->data + 16;
        for (size_t i = 0; i < count; i++) {
            put_u32(table + i * 4, sizes[i]);
        }
    }
    free(sizes);
    return ok;
}
