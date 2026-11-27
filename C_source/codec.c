#include "codec.h"
#include <string.h>
#include <zlib.h>

uint16_t codec_u16(const unsigned char *data, size_t off) {
    return (uint16_t)(data[off] | ((uint16_t)data[off + 1] << 8));
}

uint32_t codec_u32(const unsigned char *data, size_t off) {
    return (uint32_t)data[off] | ((uint32_t)data[off + 1] << 8) |
           ((uint32_t)data[off + 2] << 16) | ((uint32_t)data[off + 3] << 24);
}

size_t codec_align_up(size_t value, size_t alignment) {
    return (value + (alignment - 1)) & ~(alignment - 1);
}

int codec_looks_like_zlib_header(const unsigned char *data, size_t len, size_t off) {
    if (off + 2 > len) {
        return 0;
    }
    unsigned cmf = data[off];
    unsigned flg = data[off + 1];
    if (cmf != 0x78) {
        return 0;
    }
    return (((cmf << 8) | flg) % 31) == 0;
}

int codec_inflate(const unsigned char *data, size_t len, buf *out, err *e) {
    buf_reset(out);
    if (len == 0) {
        err_set(e, "zlib stream is empty");
        return 0;
    }

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit(&zs) != Z_OK) {
        err_set(e, "inflateInit failed");
        return 0;
    }

    zs.next_in = (Bytef *)data;
    zs.avail_in = 0;
    size_t fed = 0;

    unsigned char scratch[1u << 16];
    int status = Z_OK;

    for (;;) {
        if (zs.avail_in == 0 && fed < len) {
            size_t take = len - fed;
            if (take > (size_t)0x7FFFFFFF) {
                take = (size_t)0x7FFFFFFF;
            }
            zs.next_in = (Bytef *)(data + fed);
            zs.avail_in = (uInt)take;
            fed += take;
        }

        zs.next_out = scratch;
        zs.avail_out = (uInt)sizeof(scratch);
        status = inflate(&zs, Z_NO_FLUSH);

        size_t produced = sizeof(scratch) - zs.avail_out;
        if (produced > 0 && !buf_put(out, scratch, produced)) {
            inflateEnd(&zs);
            err_set(e, "out of memory while inflating");
            return 0;
        }

        if (status == Z_STREAM_END) {
            break;
        }
        if (status != Z_OK) {
            inflateEnd(&zs);
            err_set(e, "zlib error %d while inflating", status);
            return 0;
        }
        if (zs.avail_in == 0 && fed >= len && produced == 0) {
            inflateEnd(&zs);
            err_set(e, "zlib stream ended early");
            return 0;
        }
    }

    inflateEnd(&zs);
    return 1;
}

int codec_deflate(const unsigned char *data, size_t len, int level, buf *out, err *e) {
    buf_reset(out);

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (level < 0 || level > 9) {
        level = Z_DEFAULT_COMPRESSION;
    }
    if (deflateInit(&zs, level) != Z_OK) {
        err_set(e, "deflateInit failed");
        return 0;
    }

    zs.next_in = (Bytef *)data;
    zs.avail_in = 0;
    size_t fed = 0;

    unsigned char scratch[1u << 16];
    for (;;) {
        if (zs.avail_in == 0 && fed < len) {
            size_t take = len - fed;
            if (take > (size_t)0x7FFFFFFF) {
                take = (size_t)0x7FFFFFFF;
            }
            zs.next_in = (Bytef *)(data + fed);
            zs.avail_in = (uInt)take;
            fed += take;
        }

        int flush = (fed >= len) ? Z_FINISH : Z_NO_FLUSH;
        zs.next_out = scratch;
        zs.avail_out = (uInt)sizeof(scratch);
        int status = deflate(&zs, flush);

        size_t produced = sizeof(scratch) - zs.avail_out;
        if (produced > 0 && !buf_put(out, scratch, produced)) {
            deflateEnd(&zs);
            err_set(e, "out of memory while deflating");
            return 0;
        }
        if (status == Z_STREAM_END) {
            break;
        }
        if (status != Z_OK && status != Z_BUF_ERROR) {
            deflateEnd(&zs);
            err_set(e, "zlib error %d while deflating", status);
            return 0;
        }
    }

    deflateEnd(&zs);
    return 1;
}

int codec_zlib_header_anywhere(const unsigned char *data, size_t len, buf *out, err *e) {
    err scratch_err;

    if (len >= 6) {
        uint32_t size0 = codec_u32(data, 0);
        if (size0 > 0 && (size_t)4 + size0 <= len) {
            err_clear(&scratch_err);
            if (codec_inflate(data + 4, size0, out, &scratch_err)) {
                return 1;
            }
        }
    }

    if (len >= 2) {
        for (size_t i = 0; i + 1 < len; i++) {
            if (data[i] != 0x78) {
                continue;
            }
            if (!codec_looks_like_zlib_header(data, len, i)) {
                continue;
            }
            if (i >= 4) {
                uint32_t size = codec_u32(data, i - 4);
                if (size > 0 && i + size <= len) {
                    err_clear(&scratch_err);
                    if (codec_inflate(data + i, size, out, &scratch_err)) {
                        return 1;
                    }
                }
            }
            err_clear(&scratch_err);
            if (codec_inflate(data + i, len - i, out, &scratch_err)) {
                return 1;
            }
        }
    }

    err_set(e, "couldnt find a valid omega zlib_header stream");
    return 0;
}

static int layout_fixed_alignment(const unsigned char *data, size_t len,
                                  const uint32_t *sizes, uint16_t chunk_count,
                                  size_t header_end, split_chunk *chunks) {
    size_t ptr = codec_align_up(header_end, 0x80);
    for (uint16_t idx = 0; idx < chunk_count; idx++) {
        uint32_t chunk_size = sizes[idx];
        if (ptr + 4 > len) {
            return 0;
        }
        uint32_t inner_size = codec_u32(data, ptr);
        size_t data_start = ptr + 4;
        size_t data_end = data_start + inner_size;

        if ((uint64_t)inner_size + 4 == (uint64_t)chunk_size && data_end <= len &&
            codec_looks_like_zlib_header(data, len, data_start)) {
            chunks[idx].offset = ptr;
            chunks[idx].payload_off = data_start;
            chunks[idx].payload_size = inner_size;
            chunks[idx].compressed = 1;
            chunks[idx].table_size = chunk_size;
        } else if (idx + 1 == chunk_count && ptr + chunk_size == len) {
            data_end = ptr + chunk_size;
            chunks[idx].offset = ptr;
            chunks[idx].payload_off = ptr;
            chunks[idx].payload_size = chunk_size;
            chunks[idx].compressed = 0;
            chunks[idx].table_size = chunk_size;
        } else {
            return 0;
        }
        ptr = codec_align_up(data_end, 0x80);
    }
    return 1;
}

static int layout_scanned(const unsigned char *data, size_t len,
                          const uint32_t *sizes, uint16_t chunk_count,
                          size_t header_end, split_chunk *chunks) {
    size_t cursor = header_end;

    for (uint16_t idx = 0; idx < chunk_count; idx++) {
        uint32_t chunk_size = sizes[idx];
        uint32_t inner_expected = chunk_size - 4;
        int found = 0;

        size_t candidates[6];
        int candidate_count = 0;
        const size_t alignments[5] = {0x80, 0x40, 0x20, 0x10, 4};
        for (int a = 0; a < 5; a++) {
            size_t candidate = codec_align_up(cursor, alignments[a]);
            int seen = 0;
            for (int c = 0; c < candidate_count; c++) {
                if (candidates[c] == candidate) {
                    seen = 1;
                    break;
                }
            }
            if (!seen) {
                candidates[candidate_count++] = candidate;
            }
        }
        {
            int seen = 0;
            for (int c = 0; c < candidate_count; c++) {
                if (candidates[c] == cursor) {
                    seen = 1;
                    break;
                }
            }
            if (!seen) {
                candidates[candidate_count++] = cursor;
            }
        }

        for (int c = 0; c < candidate_count && !found; c++) {
            size_t candidate = candidates[c];
            if (candidate + 4 + inner_expected > len) {
                continue;
            }
            if (codec_u32(data, candidate) != inner_expected) {
                continue;
            }
            if (!codec_looks_like_zlib_header(data, len, candidate + 4)) {
                continue;
            }
            chunks[idx].offset = candidate;
            chunks[idx].payload_off = candidate + 4;
            chunks[idx].payload_size = inner_expected;
            chunks[idx].compressed = 1;
            chunks[idx].table_size = chunk_size;
            found = 1;
        }

        if (!found && idx + 1 == chunk_count) {
            for (int c = 0; c < candidate_count && !found; c++) {
                size_t candidate = candidates[c];
                if (candidate + chunk_size != len) {
                    continue;
                }
                chunks[idx].offset = candidate;
                chunks[idx].payload_off = candidate;
                chunks[idx].payload_size = chunk_size;
                chunks[idx].compressed = 0;
                chunks[idx].table_size = chunk_size;
                found = 1;
            }
            if (!found && len >= chunk_size) {
                size_t candidate = len - chunk_size;
                if (candidate >= cursor) {
                    chunks[idx].offset = candidate;
                    chunks[idx].payload_off = candidate;
                    chunks[idx].payload_size = chunk_size;
                    chunks[idx].compressed = 0;
                    chunks[idx].table_size = chunk_size;
                    found = 1;
                }
            }
        }

        if (!found) {
            if (len < (size_t)4 + inner_expected) {
                return 0;
            }
            size_t scan_limit = len - (4 + inner_expected);
            if (cursor + 0x4000 < scan_limit) {
                scan_limit = cursor + 0x4000;
            }
            for (size_t scan = cursor; scan <= scan_limit; scan++) {
                if (codec_u32(data, scan) == inner_expected &&
                    codec_looks_like_zlib_header(data, len, scan + 4)) {
                    chunks[idx].offset = scan;
                    chunks[idx].payload_off = scan + 4;
                    chunks[idx].payload_size = inner_expected;
                    chunks[idx].compressed = 1;
                    chunks[idx].table_size = chunk_size;
                    found = 1;
                    break;
                }
            }
        }

        if (!found) {
            return 0;
        }
        cursor = chunks[idx].payload_off + chunks[idx].payload_size;
    }
    return 1;
}

int codec_read_split_layout(const unsigned char *data, size_t len, arena *a, split_layout *out) {
    if (len < 0x0C) {
        return 0;
    }

    uint16_t unk0 = codec_u16(data, 0x00);
    uint16_t file_type = codec_u16(data, 0x02);
    uint16_t chunk_count = codec_u16(data, 0x04);
    uint16_t unk1 = codec_u16(data, 0x06);
    uint32_t total_unc = codec_u32(data, 0x08);

    if (chunk_count == 0) {
        return 0;
    }

    size_t header_end = 0x0C + (size_t)4 * chunk_count;
    if (header_end > len) {
        return 0;
    }

    uint32_t *sizes = (uint32_t *)arena_alloc(a, sizeof(uint32_t) * chunk_count);
    split_chunk *chunks = (split_chunk *)arena_alloc(a, sizeof(split_chunk) * chunk_count);
    if (sizes == NULL || chunks == NULL) {
        return 0;
    }

    for (uint16_t i = 0; i < chunk_count; i++) {
        uint32_t chunk_size = codec_u32(data, 0x0C + (size_t)4 * i);
        if (chunk_size < 4) {
            return 0;
        }
        sizes[i] = chunk_size;
    }

    if (!layout_fixed_alignment(data, len, sizes, chunk_count, header_end, chunks)) {
        if (!layout_scanned(data, len, sizes, chunk_count, header_end, chunks)) {
            return 0;
        }
    }

    int any_compressed = 0;
    for (uint16_t i = 0; i < chunk_count; i++) {
        if (chunks[i].compressed) {
            any_compressed = 1;
            break;
        }
    }
    if (!any_compressed) {
        return 0;
    }

    out->unk0 = unk0;
    out->file_type = file_type;
    out->chunk_count = chunk_count;
    out->unk1 = unk1;
    out->total_unc = total_unc;
    out->header_end = header_end;
    out->sizes = sizes;
    out->chunks = chunks;
    return 1;
}

int codec_looks_like_classic_split(const unsigned char *data, size_t len, arena *a) {
    split_layout layout;
    return codec_read_split_layout(data, len, a, &layout);
}

static int zero_span(const unsigned char *data, size_t start, size_t end) {
    for (size_t i = start; i < end; i++) {
        if (data[i] != 0) {
            return 0;
        }
    }
    return 1;
}

int codec_read_stored_layout(const unsigned char *data, size_t len, arena *a, split_layout *out) {
    if (len < 0x80) {
        return 0;
    }
    uint32_t block = codec_u32(data, 0x00);
    uint16_t chunk_count = codec_u16(data, 0x04);
    uint32_t total_unc = codec_u32(data, 0x08);
    if (block == 0 || chunk_count == 0 || codec_u16(data, 0x06) != 0 || total_unc == 0) {
        return 0;
    }
    size_t header_end = 0x0C + (size_t)4 * chunk_count;
    size_t first = codec_align_up(header_end, 0x80);
    if (first > len || !zero_span(data, header_end, first)) {
        return 0;
    }

    uint32_t *sizes = (uint32_t *)arena_alloc(a, sizeof(uint32_t) * chunk_count);
    split_chunk *chunks = (split_chunk *)arena_alloc(a, sizeof(split_chunk) * chunk_count);
    if (sizes == NULL || chunks == NULL) {
        return 0;
    }

    uint64_t sum = 0;
    size_t ptr = first;
    for (uint16_t idx = 0; idx < chunk_count; idx++) {
        uint32_t chunk_size = codec_u32(data, 0x0C + (size_t)4 * idx);
        int last = idx + 1 == chunk_count;
        if (chunk_size == 0 || chunk_size > block || (!last && chunk_size != block)) {
            return 0;
        }
        if (ptr + chunk_size > len) {
            return 0;
        }
        sizes[idx] = chunk_size;
        chunks[idx].offset = ptr;
        chunks[idx].payload_off = ptr;
        chunks[idx].payload_size = chunk_size;
        chunks[idx].compressed = 0;
        chunks[idx].table_size = chunk_size;
        sum += chunk_size;
        size_t next = codec_align_up(ptr + chunk_size, 0x80);
        if (next > len || !zero_span(data, ptr + chunk_size, next)) {
            return 0;
        }
        ptr = next;
    }
    if (sum != (uint64_t)total_unc || ptr != len) {
        return 0;
    }

    out->unk0 = codec_u16(data, 0x00);
    out->file_type = codec_u16(data, 0x02);
    out->chunk_count = chunk_count;
    out->unk1 = 0;
    out->total_unc = total_unc;
    out->header_end = header_end;
    out->sizes = sizes;
    out->chunks = chunks;
    return 1;
}

int codec_looks_like_stored_split(const unsigned char *data, size_t len, arena *a) {
    split_layout layout;
    return codec_read_stored_layout(data, len, a, &layout);
}

static int read_pairtable(const unsigned char *data, size_t len, arena *a,
                          size_t **offsets_out, size_t **sizes_out, uint32_t *count_out);

int codec_read_pairtable_entries(const unsigned char *data, size_t len, arena *a,
                                 size_t **offsets_out, size_t **sizes_out, uint32_t *count_out) {
    return read_pairtable(data, len, a, offsets_out, sizes_out, count_out);
}

static int read_pairtable(const unsigned char *data, size_t len, arena *a,
                          size_t **offsets_out, size_t **sizes_out, uint32_t *count_out) {
    if (len < 20) {
        return 0;
    }
    uint32_t count = codec_u32(data, 0x00);
    if (count < 1 || count > PAIRTABLE_MAX_COUNT) {
        return 0;
    }
    size_t table_end = 4 + (size_t)count * 8;
    if (table_end > len) {
        return 0;
    }

    size_t *offsets = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    size_t *sizes = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    if (offsets == NULL || sizes == NULL) {
        return 0;
    }

    size_t previous_end = table_end;
    for (uint32_t index = 0; index < count; index++) {
        size_t ent_off = 4 + (size_t)index * 8;
        uint32_t payload_off = codec_u32(data, ent_off);
        uint32_t payload_size = codec_u32(data, ent_off + 4);
        if (payload_size == 0) {
            return 0;
        }
        if (payload_off < table_end || (size_t)payload_off + payload_size > len) {
            return 0;
        }
        if (payload_off < previous_end) {
            return 0;
        }
        offsets[index] = payload_off;
        sizes[index] = payload_size;
        previous_end = (size_t)payload_off + payload_size;
    }

    *offsets_out = offsets;
    *sizes_out = sizes;
    *count_out = count;
    return 1;
}

int codec_looks_like_pairtable(const unsigned char *data, size_t len, arena *a) {
    size_t *offsets = NULL;
    size_t *sizes = NULL;
    uint32_t count = 0;
    if (!read_pairtable(data, len, a, &offsets, &sizes, &count)) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!codec_looks_like_classic_split(data + offsets[i], sizes[i], a)) {
            return 0;
        }
    }
    return 1;
}

int codec_looks_like_split(const unsigned char *data, size_t len, arena *a) {
    return codec_looks_like_classic_split(data, len, a) ||
           codec_looks_like_pairtable(data, len, a);
}

int codec_looks_like_empty_stub(const unsigned char *data, size_t len) {
    if (len < 0x0C) {
        return 0;
    }
    if (codec_u16(data, 0x02) == 0) {
        return 0;
    }
    if (codec_u16(data, 0x04) != 0) {
        return 0;
    }
    if (codec_u32(data, 0x08) != 0) {
        return 0;
    }
    if (data[0x00] || data[0x01] || data[0x06] || data[0x07]) {
        return 0;
    }
    for (size_t i = 0x0C; i < len; i++) {
        if (data[i]) {
            return 0;
        }
    }
    return 1;
}

int codec_read_stored_split(const unsigned char *data, size_t len, uint64_t expected, buf *out) {
    if (len < 0x0C || expected == 0) {
        return 0;
    }

    uint16_t chunk_count = codec_u16(data, 0x04);
    uint32_t total_unc = codec_u32(data, 0x08);
    if (chunk_count == 0 || (uint64_t)total_unc != expected) {
        return 0;
    }

    size_t header_end = 0x0C + (size_t)4 * chunk_count;
    if (header_end > len) {
        return 0;
    }

    uint64_t sum = 0;
    for (uint16_t i = 0; i < chunk_count; i++) {
        sum += codec_u32(data, 0x0C + (size_t)4 * i);
    }
    if (sum != (uint64_t)total_unc) {
        return 0;
    }

    buf_reset(out);
    size_t ptr = codec_align_up(header_end, 0x80);
    for (uint16_t i = 0; i < chunk_count; i++) {
        uint32_t chunk_size = codec_u32(data, 0x0C + (size_t)4 * i);
        if (chunk_size == 0 || ptr + chunk_size > len) {
            return 0;
        }
        if (!buf_put(out, data + ptr, chunk_size)) {
            return 0;
        }
        ptr = codec_align_up(ptr + chunk_size, 0x80);
    }
    return 1;
}

static const char *split_ext_for_type(uint16_t file_type) {
    if (file_type == 0x0001) {
        return ".g1m";
    }
    if (file_type == 0x0010) {
        return ".g1t";
    }
    return ".bin";
}

int codec_decompress_classic_split(const unsigned char *data, size_t len, arena *a,
                                   buf *out, const char **ext_hint, err *e) {
    split_layout layout;
    if (!codec_read_split_layout(data, len, a, &layout) &&
        !codec_read_stored_layout(data, len, a, &layout)) {
        err_set(e, "split zlib stream: structure didnt match");
        return 0;
    }

    buf_reset(out);
    buf piece;
    buf_init(&piece);

    for (uint16_t idx = 0; idx < layout.chunk_count; idx++) {
        split_chunk *chunk = &layout.chunks[idx];
        size_t data_start = chunk->payload_off;
        size_t data_end = data_start + chunk->payload_size;
        if (data_end > len) {
            buf_free(&piece);
            err_set(e, "split zlib stream: truncated chunk %u", (unsigned)idx);
            return 0;
        }

        if (chunk->compressed) {
            if (!codec_inflate(data + data_start, chunk->payload_size, &piece, e)) {
                buf_free(&piece);
                return 0;
            }
            if (!buf_put(out, piece.data, piece.len)) {
                buf_free(&piece);
                err_set(e, "out of memory merging split chunks");
                return 0;
            }
        } else {
            if (!buf_put(out, data + data_start, chunk->payload_size)) {
                buf_free(&piece);
                err_set(e, "out of memory merging split chunks");
                return 0;
            }
        }
    }

    buf_free(&piece);
    if (ext_hint != NULL) {
        *ext_hint = split_ext_for_type(layout.file_type);
    }
    return 1;
}

static int decompress_pairtable(const unsigned char *data, size_t len, arena *a,
                                buf *out, const char **ext_hint, err *e) {
    size_t *offsets = NULL;
    size_t *sizes = NULL;
    uint32_t count = 0;
    if (!read_pairtable(data, len, a, &offsets, &sizes, &count)) {
        err_set(e, "pairtable split-zlib wrapper: structure didnt match");
        return 0;
    }

    buf_reset(out);
    buf piece;
    buf_init(&piece);
    const char *chosen = NULL;

    for (uint32_t i = 0; i < count; i++) {
        const char *inner_ext = NULL;
        if (!codec_decompress_classic_split(data + offsets[i], sizes[i], a, &piece, &inner_ext, e)) {
            buf_free(&piece);
            return 0;
        }
        if (!buf_put(out, piece.data, piece.len)) {
            buf_free(&piece);
            err_set(e, "out of memory merging pairtable members");
            return 0;
        }
        if (chosen == NULL && inner_ext != NULL && strcmp(inner_ext, ".bin") != 0) {
            chosen = inner_ext;
        }
        if (i == 0 && chosen == NULL) {
            chosen = inner_ext;
        }
    }

    buf_free(&piece);
    if (ext_hint != NULL) {
        *ext_hint = chosen == NULL ? ".bin" : chosen;
    }
    return 1;
}

int codec_decompress_split(const unsigned char *data, size_t len, arena *a,
                           buf *out, const char **ext_hint, err *e) {
    err classic_err;
    err_clear(&classic_err);
    if (codec_decompress_classic_split(data, len, a, out, ext_hint, &classic_err)) {
        return 1;
    }
    if (decompress_pairtable(data, len, a, out, ext_hint, e)) {
        return 1;
    }
    err_set(e, "classic split-zlib failed: %s", classic_err.set ? classic_err.text : "unknown");
    return 0;
}

typedef struct {
    const char *magic;
    int magic_len;
    const char *ext;
} magic_rule;

static const magic_rule magic_table[] = {
    {"ALGB", 4, ".alg"}, {"GT1G", 4, ".g1t"}, {"_M1G", 4, ".g1m"}, {"_S1G", 4, ".g1s"},
    {"_S2G", 4, ".g2s"}, {"ME1G", 4, ".g1em"}, {"_E1G", 4, ".g1e"}, {"_A1G", 4, ".g1a"},
    {"_A2G", 4, ".g2a"}, {"XF1G", 4, ".g1fx"}, {"OC1G", 4, ".g1c"}, {"_L1G", 4, ".g1l"},
    {"_N1G", 4, ".g1n"}, {"_H1G", 4, ".g1h"}, {"SV1G", 4, ".g1vs"}, {"LCSK", 4, ".kscl"},
    {"TLSK", 4, ".kslt"}, {"KTSR", 4, ".ktsl2stbin"}, {"KTSC", 4, ".ktsl2asbin"},
    {"KTSS", 4, ".ktss"}, {"KOVS", 4, ".kvs"}, {"_SPK", 4, ".postfx"}, {"_OLS", 4, ".sebin"},
    {"OggS", 4, ".ogg"}, {"RIFF", 4, ".riff"}, {"1DHW", 4, ".sed"}, {"_HBW", 4, ".wbh"},
    {"_DBW", 4, ".wbd"}, {"KPMG", 4, ".gmpk"}, {"KPML", 4, ".lmpk"}, {"KPAG", 4, ".gapk"},
    {"KPEG", 4, ".gepk"}, {"0KPB", 4, ".bpk"}, {"KPTR", 4, ".rtrpk"}, {"KLMD", 4, ".mdlk"},
    {"RLDM", 4, ".mdlpack"}, {"TLDM", 4, ".mdltexpack"}, {"GRAX", 4, ".exarg"},
    {"RFFE", 4, ".effectpack"}, {"DAEH", 4, ".exhead"}, {"RRRT", 4, ".ktfkpack"},
    {"RLOC", 4, ".colpack"}, {"APDT", 4, ".tdpack"}, {"_DRK", 4, ".rdb"},
    {"IDRK", 4, ".rdb.bin"}, {"PDRK", 4, ".fdata"}, {"_RNK", 4, ".name"},
    {"IRNK", 4, ".name.bin"}, {"_DOK", 4, ".kidsobjdb"}, {"IDOK", 4, ".kidsobjdb.bin"},
    {"RDOK", 4, ".kidsobjdb.bin"}, {"MDLS", 4, ".mdls"}, {"DXBC", 4, ".dxbc"},
    {"FP1G", 4, ".fp1g"}, {"HWYX", 4, ".hwyx"}, {"SCM_", 4, ".scm"}, {"DLV0", 4, ".dlv0"},
    {"DLV4", 4, ".dlv4"}, {"SV00", 4, ".sv00"}, {"SV01", 4, ".sv01"}, {"SV02", 4, ".sv02"},
    {"SV03", 4, ".sv03"}, {"SV20", 4, ".sv20"}, {"SV30", 4, ".sv30"}, {"SV40", 4, ".sv40"},
    {"SV41", 4, ".sv41"}, {"Act_", 4, ".act"}, {"ET00", 4, ".et00"}, {"ET01", 4, ".et01"},
    {"ET02", 4, ".et02"}, {"ET03", 4, ".et03"}, {"FT02", 4, ".ft02"}, {"SARC", 4, ".sarc"},
    {"CRAE", 4, ".elixir"}, {"SPKG", 4, ".spkg"}, {"SCEN", 4, ".scene"},
    {"KPS3", 4, ".shaderpack"}, {"QGWS", 4, ".swg"}, {"EVIR", 4, ".river"},
    {"BGIR", 4, ".rig"}, {"RTRE", 4, ".ertr"}, {"DATD", 4, ".datd"}, {"D0CL", 4, ".lcd0"},
    {"HDDB", 4, ".hdb"}, {"RTXE", 4, ".extra"}, {"LLOC", 4, ".coll"}, {"ONUN", 4, ".nuno"},
    {"VNUN", 4, ".nunv"}, {"SNUN", 4, ".nuns"}, {"TFOS", 4, ".soft"}, {"RIAH", 4, ".hair"},
    {"TNOC", 4, ".cont"}, {"pkgi", 4, ".pkginfo"}, {"DDS ", 4, ".dds"},
    {"char", 4, ".chardata"}, {"clip", 4, ".clip"}, {"body", 4, ".bodybase"},
    {"MSBP", 4, ".material"}, {"tdpa", 4, ".tdpack"}, {"HIUB", 4, ".hiub"},
    {"MDLK", 4, ".mdlk"}, {"ipu2", 4, ".ipu2"}, {"MESC", 4, ".mesc"}, {"OFNI", 4, ".info"},
    {"_COK", 4, ".koc"}, {"SWGQ", 4, ".swg"}, {"DJBO", 4, ".objd"}, {"WHD1", 4, ".whd"},
    {"DMIG", 4, ".g1md"}, {"LHSK", 4, ".KSHL"},
    {"G1TG", 4, ".g1t"}, {"KLDM", 4, ".mdlk"}, {"SDF_", 4, ".sdf"},
    {"PAC0", 4, ".pac"}, {"SM4L", 4, ".idx"},
    {"COLK", 4, ".colk"}, {"MDL ", 4, ".mdl"}, {"KFTK", 4, ".ktfk"},
    {"KSHL", 4, ".KSHL"}, {"BTSC", 4, ".cstb"},
    {"zp1", 3, ".zp1"},
    {"XFT", 3, ".xft"}, {"XKM", 3, ".xkm"}, {"GT1", 3, ".g1t"},
    {"BM", 2, ".bmp"}, {"XL", 2, ".xl"},
};

static const int magic_total = (int)(sizeof(magic_table) / sizeof(magic_table[0]));

#define MAGIC_SLOTS 512

static int16_t magic_slots[MAGIC_SLOTS];
static uint32_t magic_keys[MAGIC_SLOTS];
static int magic_ready;

static uint32_t magic_word(const unsigned char *at) {
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8) |
           ((uint32_t)at[2] << 16) | ((uint32_t)at[3] << 24);
}

static void magic_build(void) {
    for (int i = 0; i < MAGIC_SLOTS; i++) {
        magic_slots[i] = -1;
    }
    for (int i = 0; i < magic_total; i++) {
        if (magic_table[i].magic_len != 4) {
            continue;
        }
        uint32_t key = magic_word((const unsigned char *)magic_table[i].magic);
        uint32_t slot = (key * 2654435761u) % MAGIC_SLOTS;
        while (magic_slots[slot] >= 0) {
            if (magic_keys[slot] == key) {
                break;
            }
            slot = (slot + 1) % MAGIC_SLOTS;
        }
        if (magic_slots[slot] < 0) {
            magic_slots[slot] = (int16_t)i;
            magic_keys[slot] = key;
        }
    }
    magic_ready = 1;
}

static int magic_swap;

void codec_set_big_endian(int big) {
    magic_swap = big ? 1 : 0;
}

int codec_big_endian(void) {
    return magic_swap;
}

void codec_init(void) {
    if (!magic_ready) {
        magic_build();
    }
}

static const magic_rule *magic_lookup_word(uint32_t key) {
    uint32_t slot = (key * 2654435761u) % MAGIC_SLOTS;
    for (int probe = 0; probe < MAGIC_SLOTS; probe++) {
        int16_t index = magic_slots[slot];
        if (index < 0) {
            return NULL;
        }
        if (magic_keys[slot] == key) {
            return &magic_table[index];
        }
        slot = (slot + 1) % MAGIC_SLOTS;
    }
    return NULL;
}

static const magic_rule *magic_lookup4(const unsigned char *at) {
    const magic_rule *hit = magic_lookup_word(magic_word(at));
    if (hit != NULL || !codec_big_endian()) {
        return hit;
    }
    unsigned char reversed[4] = {at[3], at[2], at[1], at[0]};
    return magic_lookup_word(magic_word(reversed));
}

static const magic_rule *magic_lookup_short(const unsigned char *at, size_t avail) {
    for (int i = 0; i < magic_total; i++) {
        const magic_rule *rule = &magic_table[i];
        if (rule->magic_len == 4 || (size_t)rule->magic_len > avail) {
            continue;
        }
        if (memcmp(at, rule->magic, (size_t)rule->magic_len) == 0) {
            return rule;
        }
    }
    return NULL;
}

const char *codec_dx9_ext_at(const unsigned char *data, size_t len, size_t off) {
    if (off + 12 > len) {
        return NULL;
    }
    uint32_t token = codec_u32(data, off);
    uint32_t shader_type = token & 0xFFFF0000u;
    uint32_t major = (token >> 8) & 0xFFu;

    if (shader_type != 0xFFFE0000u && shader_type != 0xFFFF0000u) {
        return NULL;
    }
    if (major == 0 || major > 3) {
        return NULL;
    }
    if (memcmp(data + off + 8, "CTAB", 4) != 0) {
        return NULL;
    }
    return shader_type == 0xFFFE0000u ? ".vsh" : ".psh";
}

const char *codec_match_ext_tables(const unsigned char *data, size_t len, size_t off) {
    if (off + 4 > len) {
        return NULL;
    }
    const magic_rule *rule = magic_lookup4(data + off);
    if (rule != NULL) {
        return rule->ext;
    }
    rule = magic_lookup_short(data + off, len - off);
    return rule == NULL ? NULL : rule->ext;
}

static const char *detect_dx9_shader_ext(const unsigned char *data, size_t len) {
    return codec_dx9_ext_at(data, len, 0);
}

static int head_contains(const unsigned char *head, size_t head_len, const char *needle) {
    size_t needle_len = strlen(needle);
    if (needle_len > head_len) {
        return 0;
    }
    for (size_t i = 0; i + needle_len <= head_len; i++) {
        if (memcmp(head + i, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

const char *codec_detect_ext(const unsigned char *data, size_t len) {
    if (len == 0) {
        return ".bin";
    }

    size_t head_len = len < 64 ? len : 64;

    const magic_rule *rule = head_len >= 4 ? magic_lookup4(data) : NULL;
    if (rule == NULL) {
        rule = magic_lookup_short(data, head_len);
    }
    if (rule != NULL) {
        if (strcmp(rule->ext, ".riff") == 0) {
            return head_contains(data, head_len, "WAVEfmt") ? ".wav" : ".riff";
        }
        return rule->ext;
    }

    const char *shader = detect_dx9_shader_ext(data, len);
    if (shader != NULL) {
        return shader;
    }

    if (head_len >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
        return ".png";
    }
    if (head_contains(data, head_len, "JFIF")) {
        return ".jpg";
    }
    if (head_len >= 4 && memcmp(data, "TIM2", 4) == 0) {
        return ".tm2";
    }
    if (head_contains(data, head_len, "TIM2")) {
        return ".tm2";
    }
    if (head_len >= 4 && memcmp(data, "\x00\x20\xAF\x30", 4) == 0) {
        return ".tm2";
    }
    if (len >= 4 && memcmp(data, "SShd", 4) == 0) {
        return ".ss2";
    }
    if (len >= 4 && memcmp(data, "SSbd", 4) == 0) {
        return ".ss2bd";
    }
    if (len >= 8 && memcmp(data, "IECSsreV", 8) == 0) {
        return ".vagbank";
    }
    if (head_len >= 4 && memcmp(data, "[glo", 4) == 0) {
        return ".ini";
    }
    if (head_len >= 4 && memcmp(data, "[hdr", 4) == 0) {
        return ".ini";
    }
    if (head_len >= 4 && memcmp(data, "\x45\x4D\x06\x00", 4) == 0) {
        return ".EM";
    }

    return ".bin";
}

const char *codec_resolve_ext(const unsigned char *data, size_t len, const char *ext_hint) {
    const char *ext = codec_detect_ext(data, len);

    if (strcmp(ext, ".ini") == 0 || strcmp(ext, ".txt") == 0) {
        size_t head_len = len < 64 ? len : 64;
        for (size_t i = 0; i < head_len; i++) {
            if (data[i] == 0) {
                return ".bin";
            }
        }
    }
    if (strcmp(ext, ".bin") != 0) {
        return ext;
    }
    if (ext_hint != NULL && strcmp(ext_hint, ".g1m") == 0 && len >= 4 &&
        memcmp(data, "\x5F\x4D\x31\x47", 4) == 0) {
        return ".g1m";
    }
    if (ext_hint != NULL && strcmp(ext_hint, ".g1t") == 0 && len >= 3 &&
        memcmp(data, "GT1", 3) == 0) {
        return ".g1t";
    }
    return ".bin";
}
