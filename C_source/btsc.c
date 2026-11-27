#include "btsc.h"
#include "codec.h"
#include <string.h>
#define BTSC_MAX_CHUNKS 0x1F8

static void put_u32(unsigned char *at, uint32_t v) {
    at[0] = (unsigned char)(v & 0xFF);
    at[1] = (unsigned char)((v >> 8) & 0xFF);
    at[2] = (unsigned char)((v >> 16) & 0xFF);
    at[3] = (unsigned char)((v >> 24) & 0xFF);
}

static size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

static size_t chunk_start(const unsigned char *data, uint32_t index, uint32_t data_start) {
    return index == 0 ? data_start : codec_u32(data, 16 + ((size_t)index - 1) * 4);
}

int btsc_looks_like(const unsigned char *data, size_t len) {
    if (len < BTSC_DATA_START + 8 || memcmp(data, "BTSC", 4) != 0) {
        return 0;
    }
    uint32_t count = codec_u32(data, 8);
    uint32_t data_start = codec_u32(data, 12);
    if (count == 0 || count > BTSC_MAX_CHUNKS || data_start < 16 + (size_t)count * 4 ||
        data_start >= len) {
        return 0;
    }
    if (codec_u32(data, 16 + ((size_t)count - 1) * 4) != 0) {
        return 0;
    }
    size_t previous = data_start;
    for (uint32_t i = 0; i < count; i++) {
        size_t start = chunk_start(data, i, data_start);
        if (start < previous || start + 6 > len) {
            return 0;
        }
        uint32_t packed = codec_u32(data, start);
        if (packed == 0 || start + 4 + (size_t)packed > len ||
            !codec_looks_like_zlib_header(data, len, start + 4)) {
            return 0;
        }
        previous = start + 4 + packed;
    }
    return 1;
}

uint32_t btsc_block_size(const unsigned char *data, size_t len) {
    if (!btsc_looks_like(data, len) || codec_u32(data, 8) < 2) {
        return BTSC_BLOCK;
    }
    buf piece;
    buf_init(&piece);
    err quiet;
    err_clear(&quiet);
    uint32_t data_start = codec_u32(data, 12);
    uint32_t packed = codec_u32(data, data_start);
    uint32_t block = BTSC_BLOCK;
    if (codec_inflate(data + data_start + 4, packed, &piece, &quiet) && piece.len > 0) {
        block = (uint32_t)piece.len;
    }
    buf_free(&piece);
    return block;
}

int btsc_decompress(const unsigned char *data, size_t len, buf *out, err *e) {
    if (!btsc_looks_like(data, len)) {
        err_set(e, "not a BTSC stream");
        return 0;
    }
    uint32_t total = codec_u32(data, 4);
    uint32_t count = codec_u32(data, 8);
    uint32_t data_start = codec_u32(data, 12);

    buf_reset(out);
    if (!buf_reserve(out, total)) {
        err_set(e, "out of memory for %u decompressed bytes", total);
        return 0;
    }
    buf piece;
    buf_init(&piece);
    for (uint32_t i = 0; i < count; i++) {
        size_t start = chunk_start(data, i, data_start);
        uint32_t packed = codec_u32(data, start);
        if (!codec_inflate(data + start + 4, packed, &piece, e)) {
            buf_free(&piece);
            return 0;
        }
        if (piece.len > 0 && !buf_put(out, piece.data, piece.len)) {
            buf_free(&piece);
            err_set(e, "out of memory joining a BTSC stream");
            return 0;
        }
    }
    buf_free(&piece);
    if (out->len != total) {
        err_set(e, "BTSC stream inflated to %zu bytes but declares %u", out->len, total);
        return 0;
    }
    return 1;
}

int btsc_compress(const unsigned char *data, size_t len, uint32_t block, buf *out, err *e) {
    if (block == 0) {
        block = BTSC_BLOCK;
    }
    size_t count = len == 0 ? 1 : (len + block - 1) / block;
    if (count > BTSC_MAX_CHUNKS || len > 0xFFFFFFFFu) {
        err_set(e, "%zu bytes needs %zu BTSC chunks, more than the header holds", len, count);
        return 0;
    }

    buf_reset(out);
    unsigned char head[16];
    memcpy(head, "BTSC", 4);
    put_u32(head + 4, (uint32_t)len);
    put_u32(head + 8, (uint32_t)count);
    put_u32(head + 12, BTSC_DATA_START);
    if (!buf_put(out, head, sizeof(head))) {
        err_set(e, "out of memory starting a BTSC stream");
        return 0;
    }
    while (out->len < BTSC_DATA_START) {
        if (!buf_putc(out, 0)) {
            err_set(e, "out of memory padding a BTSC header");
            return 0;
        }
    }

    buf piece;
    buf_init(&piece);
    int ok = 1;
    for (size_t i = 0; i < count && ok; i++) {
        size_t start = i * block;
        size_t take = len - start < block ? len - start : block;
        if (i > 0) {
            put_u32((unsigned char *)out->data + 16 + (i - 1) * 4, (uint32_t)out->len);
        }
        if (!codec_deflate(data + start, take, 9, &piece, e)) {
            ok = 0;
            break;
        }
        unsigned char prefix[4];
        put_u32(prefix, (uint32_t)piece.len);
        ok = buf_put(out, prefix, 4) && buf_put(out, piece.data, piece.len);
        size_t padded = align_up(out->len, BTSC_ALIGN);
        while (ok && out->len < padded) {
            ok = buf_putc(out, 0);
        }
        if (!ok) {
            err_set(e, "out of memory writing a BTSC chunk");
        }
    }
    buf_free(&piece);
    return ok;
}
