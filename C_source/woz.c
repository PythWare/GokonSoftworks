#include "woz.h"
#include "lzp2.h"
#include <string.h>

static uint32_t woz_u32(const unsigned char *at) {
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8) | ((uint32_t)at[2] << 16) | ((uint32_t)at[3] << 24);
}

static int woz_put_u32(buf *out, uint32_t v) {
    unsigned char raw[4] = {(unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF),
                            (unsigned char)((v >> 16) & 0xFF), (unsigned char)((v >> 24) & 0xFF)};
    return buf_put(out, raw, 4);
}

int woz_pack_entry(const unsigned char *original, size_t original_len, buf *chunk, err *e) {
    int packed = lzp2_looks_like(original, original_len);
    buf work;
    buf_init(&work);
    err quiet = {0};
    if (packed && lzp2_decompress_chain(original, original_len, &work, &quiet) &&
        work.len == chunk->len && memcmp(work.data, chunk->data, chunk->len) == 0) {
        buf_reset(chunk);
        int ok = buf_put(chunk, original, original_len);
        buf_free(&work);
        if (!ok) {
            err_set(e, "out of memory restoring a WOZ entry");
        }
        return ok;
    }
    buf_reset(&work);
    float version = packed ? lzp2_version(original, original_len) : WOZ_LZP2_VERSION;
    if (!lzp2_compress_chain((const unsigned char *)chunk->data, chunk->len, version,
                             LZP2_CHUNK, &work, &quiet)) {
        buf_free(&work);
        err_set(e, "couldnt compress an edited WOZ entry: %s", quiet.set ? quiet.text : "unknown");
        return 0;
    }
    buf_reset(chunk);
    int ok = buf_put(chunk, work.data, work.len);
    buf_free(&work);
    if (!ok) {
        err_set(e, "out of memory packing a WOZ entry");
    }
    return ok;
}

int woz_chunk_list(const unsigned char *data, size_t len, buf *table, err *e) {
    size_t at = 0;
    int streams = 0;
    while (at + LZP2_HEADER <= len && lzp2_looks_like(data + at, len - at)) {
        uint32_t compressed = woz_u32(data + at + 12);
        uint64_t whole = (uint64_t)LZP2_HEADER + compressed;
        if (compressed == 0 || at + whole > len) {
            err_set(e, "a WOZ LZP2 stream at %zu runs past its entry", at);
            return 0;
        }
        if (!woz_put_u32(table, (uint32_t)whole)) {
            err_set(e, "out of memory building the WOZ chunk table");
            return 0;
        }
        streams++;
        at = (size_t)((at + whole + LZP2_ALIGN - 1) / LZP2_ALIGN * LZP2_ALIGN);
    }
    if (streams == 0) {
        err_set(e, "a compressed WOZ entry holds no LZP2 stream");
        return 0;
    }
    if (!woz_put_u32(table, 0)) {
        err_set(e, "out of memory building the WOZ chunk table");
        return 0;
    }
    return 1;
}
