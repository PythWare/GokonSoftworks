#include "lzp2.h"
#include <stdlib.h>
#include <string.h>
#define LZP2_MIN_MATCH 3
#define LZP2_MAX_MATCH 18
#define LZP2_MAX_OFFSET 2048
#define LZP2_MAX_RLE 16387
#define LZP2_MAX_LITERAL 63

static uint32_t read_u32le(const unsigned char *at) {
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8) |
           ((uint32_t)at[2] << 16) | ((uint32_t)at[3] << 24);
}

static void write_u32le(unsigned char *at, uint32_t v) {
    at[0] = (unsigned char)(v & 0xFF);
    at[1] = (unsigned char)((v >> 8) & 0xFF);
    at[2] = (unsigned char)((v >> 16) & 0xFF);
    at[3] = (unsigned char)((v >> 24) & 0xFF);
}

int lzp2_looks_like(const unsigned char *data, size_t len) {
    if (len < LZP2_HEADER || memcmp(data, "LZP2", 4) != 0) {
        return 0;
    }
    float v = lzp2_version(data, len);
    return v >= 1.0f && v < 2.0f;
}

uint32_t lzp2_original_size(const unsigned char *data, size_t len) {
    return len >= LZP2_HEADER ? read_u32le(data + 8) : 0;
}

float lzp2_version(const unsigned char *data, size_t len) {
    if (len < LZP2_HEADER) {
        return 0.0f;
    }
    uint32_t bits = read_u32le(data + 4);
    float out;
    memcpy(&out, &bits, sizeof(out));
    return out;
}

static int decompress_core(const unsigned char *data, size_t len, buf *out,
                           size_t want, err *e) {
    if (!lzp2_looks_like(data, len)) {
        err_set(e, "not an LZP2 stream");
        return 0;
    }
    uint32_t original = read_u32le(data + 8);

    size_t target = want > 0 && want < original ? want : original;
    buf_reset(out);
    if (!buf_reserve(out, target)) {
        err_set(e, "out of memory for %zu decompressed bytes", target);
        return 0;
    }

    size_t at = LZP2_HEADER;
    while (at < len && out->len < target) {
        unsigned char cmd = data[at];

        if (cmd & 0x80) {
            if (at + 2 > len) {
                break;
            }
            size_t length = (size_t)((cmd >> 3) & 0x0F) + LZP2_MIN_MATCH;
            size_t offset = (size_t)(((cmd & 0x07) << 8) | data[at + 1]) + 1;
            if (offset > out->len) {
                err_set(e, "LZP2 reference reaches %zu bytes back with only %zu written",
                        offset, out->len);
                return 0;
            }
            for (size_t i = 0; i < length; i++) {
                unsigned char b = (unsigned char)out->data[out->len - offset];
                if (!buf_putc(out, (char)b)) {
                    err_set(e, "out of memory expanding an LZP2 reference");
                    return 0;
                }
            }
            at += 2;
        } else if (cmd & 0x40) {
            if (at + 3 > len) {
                break;
            }
            size_t count = (size_t)(((cmd & 0x3F) << 8) | data[at + 1]) + 4;
            unsigned char value = data[at + 2];
            for (size_t i = 0; i < count; i++) {
                if (!buf_putc(out, (char)value)) {
                    err_set(e, "out of memory expanding an LZP2 run");
                    return 0;
                }
            }
            at += 3;
        } else {
            size_t run = cmd;
            at += 1;
            if (run > len - at) {
                run = len - at;
            }
            if (run > 0 && !buf_put(out, data + at, run)) {
                err_set(e, "out of memory copying an LZP2 literal");
                return 0;
            }
            at += run;
        }
    }

    if (out->len < target) {
        err_set(e, "LZP2 stream ended early: %zu of %zu bytes", out->len, target);
        return 0;
    }
    out->len = target;
    return 1;
}

int lzp2_decompress(const unsigned char *data, size_t len, buf *out, err *e) {
    return decompress_core(data, len, out, 0, e);
}

int lzp2_decompress_prefix(const unsigned char *data, size_t len, buf *out,
                           size_t want, err *e) {
    return decompress_core(data, len, out, want, e);
}

static size_t chain_step(const unsigned char *data, size_t at) {
    uint32_t compressed = read_u32le(data + at + 12);
    if (compressed == 0) {
        return 0;
    }
    size_t end = at + LZP2_HEADER + (size_t)compressed;
    return (end + LZP2_ALIGN - 1) / LZP2_ALIGN * LZP2_ALIGN;
}

uint32_t lzp2_chain_original(const unsigned char *data, size_t len) {
    uint64_t total = 0;
    size_t at = 0;
    while (at + LZP2_HEADER <= len && lzp2_looks_like(data + at, len - at)) {
        total += read_u32le(data + at + 8);
        size_t next = chain_step(data, at);
        if (next <= at) {
            break;
        }
        at = next;
    }
    return (uint32_t)total;
}

int lzp2_chain_streams(const unsigned char *data, size_t len) {
    int count = 0;
    size_t at = 0;
    while (at + LZP2_HEADER <= len && lzp2_looks_like(data + at, len - at)) {
        count++;
        size_t next = chain_step(data, at);
        if (next <= at) {
            break;
        }
        at = next;
    }
    return count;
}

int lzp2_decompress_chain(const unsigned char *data, size_t len, buf *out, err *e) {
    buf_reset(out);
    buf piece;
    buf_init(&piece);

    size_t at = 0;
    int any = 0;
    while (at + LZP2_HEADER <= len && lzp2_looks_like(data + at, len - at)) {
        if (!decompress_core(data + at, len - at, &piece, 0, e)) {
            buf_free(&piece);
            return 0;
        }
        if (piece.len > 0 && !buf_put(out, piece.data, piece.len)) {
            buf_free(&piece);
            err_set(e, "out of memory joining an LZP2 chain");
            return 0;
        }
        any = 1;
        size_t next = chain_step(data, at);
        if (next <= at) {
            break;
        }
        at = next;
    }

    buf_free(&piece);
    if (!any) {
        err_set(e, "not an LZP2 stream");
        return 0;
    }
    return 1;
}

uint32_t lzp2_chain_chunk(const unsigned char *data, size_t len) {
    if (lzp2_chain_streams(data, len) < 2) {
        return 0;
    }
    return read_u32le(data + 8);
}

int lzp2_compress_chain(const unsigned char *data, size_t len, float version,
                        size_t chunk, buf *out, err *e) {
    buf_reset(out);
    buf piece;
    buf_init(&piece);

    size_t at = 0;
    do {
        size_t take = len - at;
        if (chunk > 0 && take > chunk) {
            take = chunk;
        }
        if (!lzp2_compress(data + at, take, version, &piece, e)) {
            buf_free(&piece);
            return 0;
        }
        if (!buf_put(out, piece.data, piece.len)) {
            buf_free(&piece);
            err_set(e, "out of memory writing an LZP2 chain");
            return 0;
        }
        at += take;
        if (at < len) {
            size_t pad = (LZP2_ALIGN - (out->len % LZP2_ALIGN)) % LZP2_ALIGN;
            for (size_t i = 0; i < pad; i++) {
                if (!buf_putc(out, 0)) {
                    buf_free(&piece);
                    err_set(e, "out of memory padding an LZP2 chain");
                    return 0;
                }
            }
        }
    } while (at < len);

    buf_free(&piece);
    return 1;
}


#define HASH_BITS 13
#define HASH_SIZE (1u << HASH_BITS)
#define NO_POS ((int32_t)-1)

static uint32_t hash3(const unsigned char *p) {
    return (((uint32_t)p[0] << 16) ^ ((uint32_t)p[1] << 8) ^ (uint32_t)p[2]) &
           (HASH_SIZE - 1);
}

static void find_match(const unsigned char *data, size_t len, size_t pos,
                       const int32_t *table, const int32_t *chain,
                       size_t *best_len, size_t *best_off) {
    *best_len = 0;
    *best_off = 0;
    if (pos + LZP2_MIN_MATCH > len) {
        return;
    }
    uint32_t h = hash3(data + pos);
    int32_t cand = table[h];
    int guard = 0;
    while (cand != NO_POS && guard++ < 192) {
        size_t back = pos - (size_t)cand;
        if (back == 0 || back > LZP2_MAX_OFFSET) {
            break;
        }
        size_t n = 0;
        while (n < LZP2_MAX_MATCH && pos + n < len &&
               data[(size_t)cand + n] == data[pos + n]) {
            n++;
        }
        if (n > *best_len) {
            *best_len = n;
            *best_off = back;
            if (n == LZP2_MAX_MATCH) {
                break;
            }
        }
        cand = chain[cand];
    }
}

static size_t run_length(const unsigned char *data, size_t len, size_t pos) {
    unsigned char v = data[pos];
    size_t n = 1;
    while (pos + n < len && data[pos + n] == v && n < LZP2_MAX_RLE) {
        n++;
    }
    return n;
}

int lzp2_compress(const unsigned char *data, size_t len, float version,
                  buf *out, err *e) {
    buf_reset(out);

    unsigned char head[LZP2_HEADER];
    memcpy(head, "LZP2", 4);
    uint32_t bits;
    memcpy(&bits, &version, sizeof(bits));
    write_u32le(head + 4, bits);
    write_u32le(head + 8, (uint32_t)len);
    write_u32le(head + 12, 0);
    if (!buf_put(out, head, LZP2_HEADER)) {
        err_set(e, "out of memory starting an LZP2 stream");
        return 0;
    }

    int32_t *table = (int32_t *)malloc(sizeof(int32_t) * HASH_SIZE);
    int32_t *chain = len > 0 ? (int32_t *)malloc(sizeof(int32_t) * len) : NULL;
    if (table == NULL || (len > 0 && chain == NULL)) {
        free(table);
        free(chain);
        err_set(e, "out of memory indexing %zu bytes for LZP2", len);
        return 0;
    }
    for (size_t i = 0; i < HASH_SIZE; i++) {
        table[i] = NO_POS;
    }

    size_t pos = 0;
    int ok = 1;
    while (pos < len && ok) {
        size_t rle = run_length(data, len, pos);

        size_t best_len = 0;
        size_t best_off = 0;
        find_match(data, len, pos, table, chain, &best_len, &best_off);

        if (best_len >= LZP2_MIN_MATCH && best_len < LZP2_MAX_MATCH &&
            pos + 1 < len && rle < 4) {
            size_t ahead_len = 0;
            size_t ahead_off = 0;
            find_match(data, len, pos + 1, table, chain, &ahead_len, &ahead_off);
            if (ahead_len > best_len + 1) {
                best_len = 0;
            }
        }

        size_t taken;
        if (rle >= 4 && rle >= best_len) {
            size_t count = rle - 4;
            unsigned char cmd = (unsigned char)(0x40 | ((count >> 8) & 0x3F));
            unsigned char lo = (unsigned char)(count & 0xFF);
            ok = buf_putc(out, (char)cmd) && buf_putc(out, (char)lo) &&
                 buf_putc(out, (char)data[pos]);
            taken = rle;
        } else if (best_len >= LZP2_MIN_MATCH) {
            size_t code = best_off - 1;
            unsigned char cmd = (unsigned char)(0x80 |
                                                (((best_len - LZP2_MIN_MATCH) & 0x0F) << 3) |
                                                ((code >> 8) & 0x07));
            ok = buf_putc(out, (char)cmd) && buf_putc(out, (char)(code & 0xFF));
            taken = best_len;
        } else {
            size_t run = 1;
            while (run < LZP2_MAX_LITERAL && pos + run < len) {
                if (run_length(data, len, pos + run) >= 4) {
                    break;
                }
                size_t ahead_len = 0;
                size_t ahead_off = 0;
                find_match(data, len, pos + run, table, chain, &ahead_len, &ahead_off);
                if (ahead_len >= LZP2_MIN_MATCH) {
                    break;
                }
                run++;
            }
            ok = buf_putc(out, (char)run) && buf_put(out, data + pos, run);
            taken = run;
        }

        for (size_t i = 0; i < taken && pos + i + LZP2_MIN_MATCH <= len; i++) {
            size_t at = pos + i;
            uint32_t h = hash3(data + at);
            chain[at] = table[h];
            table[h] = (int32_t)at;
        }
        pos += taken;
    }

    free(table);
    free(chain);
    if (!ok) {
        err_set(e, "out of memory writing an LZP2 stream");
        return 0;
    }

    size_t payload = out->len - LZP2_HEADER;
    size_t pad = (16 - (payload % 16)) % 16;
    for (size_t i = 0; i < pad; i++) {
        if (!buf_putc(out, 0)) {
            err_set(e, "out of memory padding an LZP2 stream");
            return 0;
        }
    }
    write_u32le((unsigned char *)out->data + 12, (uint32_t)(payload + pad));
    return 1;
}
