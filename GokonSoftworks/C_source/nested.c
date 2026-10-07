#include "nested.h"
#include "codec.h"
#include "zp1.h"
#include "btsc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct scratch_node {
    arena a;
    struct scratch_node *next;
} scratch_node;

static _Thread_local scratch_node *scratch_free_list;

void nested_thread_cleanup(void) {
    scratch_node *node = scratch_free_list;
    scratch_free_list = NULL;
    while (node != NULL) {
        scratch_node *next = node->next;
        arena_free(&node->a);
        free(node);
        node = next;
    }
}

static scratch_node *scratch_get(void) {
    scratch_node *node = scratch_free_list;
    if (node != NULL) {
        scratch_free_list = node->next;
        arena_reset(&node->a);
        return node;
    }
    node = (scratch_node *)malloc(sizeof(scratch_node));
    if (node == NULL) {
        return NULL;
    }
    arena_init(&node->a);
    node->next = NULL;
    return node;
}

static void scratch_put(scratch_node *node) {
    if (node == NULL) {
        return;
    }
    node->next = scratch_free_list;
    scratch_free_list = node;
}

size_t nested_find_magic(const unsigned char *blob, size_t len, size_t from, const char *magic) {
    if (len < 4 || from + 4 > len) {
        return (size_t)-1;
    }
    const unsigned char first = (unsigned char)magic[0];
    size_t i = from;
    while (i + 4 <= len) {
        const unsigned char *hit = (const unsigned char *)memchr(blob + i, first, len - i - 3);
        if (hit == NULL) {
            return (size_t)-1;
        }
        size_t at = (size_t)(hit - blob);
        if (memcmp(blob + at, magic, 4) == 0) {
            return at;
        }
        i = at + 1;
    }
    return (size_t)-1;
}

static uint32_t word_at(const unsigned char *data, size_t off, int big) {
    if (!big) {
        return codec_u32(data, off);
    }
    return ((uint32_t)data[off] << 24) | ((uint32_t)data[off + 1] << 16) |
           ((uint32_t)data[off + 2] << 8) | (uint32_t)data[off + 3];
}

static int cmp_size(const void *left, const void *right) {
    size_t a = *(const size_t *)left;
    size_t b = *(const size_t *)right;
    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

static int all_zero(const unsigned char *blob, size_t start, size_t end) {
    for (size_t i = start; i < end; i++) {
        if (blob[i]) {
            return 0;
        }
    }
    return 1;
}

static int is_zlib_header_pair(const unsigned char *blob, size_t len, size_t off) {
    if (off + 2 > len) {
        return 0;
    }
    unsigned cmf = blob[off];
    unsigned flg = blob[off + 1];
    if ((cmf & 0x0F) != 8 || (cmf >> 4) > 7) {
        return 0;
    }
    return (((cmf << 8) + flg) % 31) == 0;
}

static int structure_one_way(const unsigned char *raw, size_t n, int big) {
    if (n < 12) {
        return 0;
    }

    uint32_t count = word_at(raw, 0, big);

    if (count >= 1 && count <= NESTED_MAX_COUNT) {
        size_t pair_table_end = 4 + (size_t)count * 8;
        if (pair_table_end <= n) {
            size_t positive = 0;
            int64_t last_off = -1;
            int valid = 1;
            for (uint32_t idx = 0; idx < count; idx++) {
                size_t ent_off = 4 + (size_t)idx * 8;
                uint32_t off = word_at(raw, ent_off, big);
                uint32_t sz = word_at(raw, ent_off + 4, big);
                if (sz == 0) {
                    continue;
                }
                if (off < pair_table_end || (size_t)off + sz > n || (int64_t)off < last_off) {
                    valid = 0;
                    break;
                }
                last_off = (int64_t)off;
                positive++;
            }
            if (valid && positive > 0) {
                return 1;
            }
        }
    }

    if (count >= 2) {
        size_t toc_table_end = 4 + (size_t)count * 4;
        if (toc_table_end <= n) {
            size_t valid_offsets = 0;
            uint64_t sum = 0;
            for (uint32_t idx = 0; idx < count; idx++) {
                uint32_t off = word_at(raw, 4 + (size_t)idx * 4, big);
                if ((size_t)off >= toc_table_end && (size_t)off < n) {
                    valid_offsets++;
                }
                sum += off;
            }
            if (valid_offsets >= 2) {
                return 1;
            }
            if (sum > 0 && toc_table_end + sum <= n) {
                return 1;
            }
        }
    }

    return 0;
}

int nested_looks_like_structure(const unsigned char *raw, size_t n) {
    if (codec_big_endian() && structure_one_way(raw, n, 1)) {
        return 1;
    }
    return structure_one_way(raw, n, 0);
}

const char *nested_match_known_signature(const unsigned char *data, size_t len, size_t off) {
    if (off + 4 > len) {
        return NULL;
    }

    const char *hit = codec_match_ext_tables(data, len, off);
    if (hit != NULL) {
        return hit;
    }

    hit = codec_dx9_ext_at(data, len, off);
    if (hit != NULL) {
        return hit;
    }

    if (off + 12 <= len) {
        int big = codec_big_endian();
        uint32_t total_out = word_at(data, off, big);
        uint32_t csize = word_at(data, off + 4, big);
        if (total_out > 0 && total_out <= 0x40000000u &&
            csize > 0 && (size_t)csize <= len - (off + 8)) {
            if (is_zlib_header_pair(data, len, off + 8)) {
                return "zl";
            }
        }
    }

    scratch_node *probe = scratch_get();
    int split = probe != NULL && codec_looks_like_split(data + off, len - off, &probe->a);
    scratch_put(probe);
    if (split) {
        return ".bin";
    }
    if (nested_looks_like_structure(data + off, len - off)) {
        return ".bin";
    }

    return NULL;
}

int nested_payload_looks_meaningful(const unsigned char *raw, size_t len, int allow_split_wrapper) {
    if (len == 0) {
        return 0;
    }
    if (nested_match_known_signature(raw, len, 0) != NULL) {
        return 1;
    }
    if (nested_looks_like_structure(raw, len)) {
        return 1;
    }
    if (allow_split_wrapper) {
        scratch_node *probe = scratch_get();
        int hit = probe != NULL &&
                  (codec_looks_like_split(raw, len, &probe->a) || codec_looks_like_pairtable(raw, len, &probe->a));
        scratch_put(probe);
        if (hit) {
            return 1;
        }
    }
    return 0;
}

static int read_subcontainer_toc(const unsigned char *data, size_t n, arena *a,
                                 uint32_t *count_out, size_t **offsets_out,
                                 size_t *table_end_out, int big) {
    if (n < 8) {
        return 0;
    }
    uint32_t count = word_at(data, 0, big);
    if (count < 2 || count > NESTED_MAX_COUNT) {
        return 0;
    }
    size_t table_end = 4 + (size_t)count * 4;
    if (table_end > n) {
        return 0;
    }
    size_t *offsets = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    if (offsets == NULL) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        offsets[i] = word_at(data, 4 + (size_t)i * 4, big);
    }
    *count_out = count;
    *offsets_out = offsets;
    *table_end_out = table_end;
    return 1;
}

static int is_real_subcontainer(const unsigned char *raw, size_t n, const size_t *offsets,
                                uint32_t count, size_t table_end) {
    size_t uniq[8];
    size_t uniq_count = 0;

    size_t *sorted = (size_t *)malloc(sizeof(size_t) * count);
    if (sorted == NULL) {
        return 0;
    }
    size_t kept = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (offsets[i] >= table_end && offsets[i] < n) {
            sorted[kept++] = offsets[i];
        }
    }
    qsort(sorted, kept, sizeof(size_t), cmp_size);

    size_t distinct = 0;
    for (size_t i = 0; i < kept; i++) {
        if (i > 0 && sorted[i] == sorted[i - 1]) {
            continue;
        }
        distinct++;
        if (uniq_count < 8) {
            uniq[uniq_count++] = sorted[i];
        }
    }
    free(sorted);

    if (distinct < 2) {
        return 0;
    }

    int hits = 0;
    for (size_t i = 0; i < uniq_count; i++) {
        if (nested_match_known_signature(raw, n, uniq[i]) != NULL) {
            hits++;
        }
    }
    return hits >= 2;
}

static size_t read_g1_resource_span(const unsigned char *blob, size_t len, size_t off) {
    if (off + 4 > len) {
        return 0;
    }
    size_t size = 0;
    int big = codec_big_endian();
    if (memcmp(blob + off, "_M1G", 4) == 0) {
        if (off + 12 > len) {
            return 0;
        }
        size = codec_u32(blob, off + 8);
    } else if (memcmp(blob + off, "OC1G", 4) == 0) {
        if (off + 0x10 > len) {
            return 0;
        }
        size = codec_u32(blob, off + 0x0C);
    } else if (big && memcmp(blob + off, "G1M_", 4) == 0) {
        if (off + 12 > len) {
            return 0;
        }
        size = word_at(blob, off + 8, 1);
    } else if (big && memcmp(blob + off, "G1CO", 4) == 0) {
        if (off + 0x10 > len) {
            return 0;
        }
        size = word_at(blob, off + 0x0C, 1);
    } else {
        return 0;
    }
    if (size == 0 || off + size > len) {
        return 0;
    }
    return size;
}

static int offsets_point_inside_known_spans(const unsigned char *blob, size_t len,
                                            const size_t *offsets, uint32_t count, size_t table_end) {
    size_t *cands = (size_t *)malloc(sizeof(size_t) * count);
    if (cands == NULL) {
        return 0;
    }
    size_t kept = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (offsets[i] >= table_end && offsets[i] < len) {
            cands[kept++] = offsets[i];
        }
    }
    if (kept == 0) {
        free(cands);
        return 0;
    }
    qsort(cands, kept, sizeof(size_t), cmp_size);

    static const char *magics[4] = {"MDLK", "LHSK", "_M1G", "OC1G"};
    int found = 0;

    for (int m = 0; m < 4 && !found; m++) {
        size_t search = 0;
        for (;;) {
            size_t off = nested_find_magic(blob, len, search, magics[m]);
            if (off == (size_t)-1) {
                break;
            }
            size_t size = 0;
            if (m == 0) {
                scratch_node *node = scratch_get();
                mdlk_layout layout;
                if (node != NULL && nested_read_mdlk_layout(blob + off, len - off, &node->a, &layout)) {
                    size = layout.payload_end;
                }
                scratch_put(node);
            } else if (m == 1) {
                scratch_node *node = scratch_get();
                kshl_layout layout;
                if (node != NULL && nested_read_kshl_layout(blob + off, len - off, &node->a, &layout)) {
                    size = layout.size;
                }
                scratch_put(node);
            } else {
                size = read_g1_resource_span(blob, len, off);
            }

            if (size > 0) {
                size_t start = off;
                size_t end = off + size;
                for (size_t i = 0; i < kept; i++) {
                    if (cands[i] > start && cands[i] < end) {
                        found = 1;
                        break;
                    }
                }
                if (found) {
                    break;
                }
                search = off + size;
            } else {
                search = off + 1;
            }
        }
    }

    free(cands);
    return found;
}

static size_t choose_sequential_data_start(const unsigned char *blob, size_t n, size_t table_end,
                                           const size_t *sizes, size_t count, int big) {
    uint64_t need = 0;
    for (size_t i = 0; i < count; i++) {
        need += sizes[i];
    }
    if (table_end + need > n) {
        return table_end;
    }

    size_t candidates[2];
    size_t candidate_count = 0;
    candidates[candidate_count++] = table_end;

    size_t scan_limit = n < table_end + 0x4000 ? n : table_end + 0x4000;
    for (size_t off = table_end; off < scan_limit; off += 4) {
        if (off + 4 > n) {
            break;
        }
        if (word_at(blob, off, big) != 0) {
            candidates[candidate_count++] = off;
            break;
        }
    }

    size_t best = table_end;
    int best_score = -1;
    for (size_t c = 0; c < candidate_count; c++) {
        size_t cand = candidates[c];
        if (cand < table_end || cand + need > n) {
            continue;
        }
        int score = 0;
        size_t cur = cand;
        size_t limit = count < 6 ? count : 6;
        for (size_t i = 0; i < limit; i++) {
            size_t sz = sizes[i];
            if (sz == 0 || cur + sz > n) {
                break;
            }
            if (nested_payload_looks_meaningful(blob + cur, sz, 0)) {
                score++;
            }
            cur += sz;
        }
        if (score > best_score) {
            best_score = score;
            best = cand;
        }
    }
    return best;
}

static int read_sequential_layout(const unsigned char *blob, size_t n, arena *a,
                                  sub_layout *out, int big) {
    if (n < 8) {
        return 0;
    }
    uint32_t count = word_at(blob, 0, big);
    if (count < 2 || count > NESTED_MAX_COUNT) {
        return 0;
    }
    size_t table_end = 4 + (size_t)count * 4;
    if (table_end > n) {
        return 0;
    }

    size_t *sizes = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    if (sizes == NULL) {
        return 0;
    }
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        sizes[i] = word_at(blob, 4 + (size_t)i * 4, big);
        total += sizes[i];
    }
    if (total == 0) {
        return 0;
    }

    size_t data_start = choose_sequential_data_start(blob, n, table_end, sizes, count, big);
    if (data_start + total > n) {
        return 0;
    }

    int hits = 0;
    int checked = 0;
    size_t nonzero = 0;
    size_t cur = data_start;
    for (uint32_t i = 0; i < count; i++) {
        size_t sz = sizes[i];
        if (sz == 0) {
            continue;
        }
        nonzero++;
        if (cur + sz > n) {
            return 0;
        }
        if (checked < 8) {
            if (nested_payload_looks_meaningful(blob + cur, sz, 0)) {
                hits++;
            }
            checked++;
        }
        cur += sz;
    }
    if (nonzero < 2 || hits < 2) {
        return 0;
    }

    memset(out, 0, sizeof(*out));
    out->kind = LAYOUT_SEQUENTIAL;
    out->big_endian = big;
    out->count = count;
    out->seq_count = count;
    out->seq_sizes = sizes;
    out->table_end = table_end;
    out->data_start = data_start;
    return 1;
}

static int empty_slots_follow_data(const size_t *offs, const size_t *sizes, uint32_t count,
                                   size_t table_end) {
    size_t running_end = table_end;
    for (uint32_t idx = 0; idx < count; idx++) {
        if (sizes[idx] != 0) {
            running_end = offs[idx] + sizes[idx];
            continue;
        }
        if (offs[idx] != running_end) {
            return 0;
        }
    }
    return 1;
}

static int read_pairtable_layout(const unsigned char *blob, size_t n, arena *a,
                                 sub_layout *out, int big) {
    if (n < 12) {
        return 0;
    }
    uint32_t count = word_at(blob, 0, big);
    if (count < 1 || count > NESTED_MAX_COUNT) {
        return 0;
    }
    size_t table_end = 4 + (size_t)count * 8;
    if (table_end > n) {
        return 0;
    }

    size_t *offs = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    size_t *sizes = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    size_t *positive = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    if (offs == NULL || sizes == NULL || positive == NULL) {
        return 0;
    }

    size_t positive_count = 0;
    int checked = 0;
    int hits = 0;
    int64_t last_off = -1;
    int first_positive_meaningful = 0;

    for (uint32_t idx = 0; idx < count; idx++) {
        size_t ent_off = 4 + (size_t)idx * 8;
        size_t off = word_at(blob, ent_off, big);
        size_t sz = word_at(blob, ent_off + 4, big);
        offs[idx] = off;
        sizes[idx] = sz;

        if (sz == 0) {
            continue;
        }
        if (off < table_end || off + sz > n) {
            return 0;
        }
        if (last_off > (int64_t)off) {
            return 0;
        }
        last_off = (int64_t)off;

        if (checked < 8) {
            int meaningful = nested_payload_looks_meaningful(blob + off, sz, 0);
            if (meaningful) {
                hits++;
                if (positive_count == 0) {
                    first_positive_meaningful = 1;
                }
            }
            checked++;
        }
        positive[positive_count++] = idx;
    }

    if (positive_count == 0) {
        return 0;
    }

    if (positive_count == 1) {
        size_t idx = positive[0];
        if (!nested_payload_looks_meaningful(blob + offs[idx], sizes[idx], 0)) {
            return 0;
        }
    } else if (hits < 2) {
        size_t first_off = offs[positive[0]];
        size_t payload_end = 0;
        int contiguous = 1;
        for (size_t i = 0; i < positive_count; i++) {
            size_t off = offs[positive[i]];
            size_t sz = sizes[positive[i]];
            if (off + sz > payload_end) {
                payload_end = off + sz;
            }
            if (i + 1 < positive_count) {
                size_t next_off = offs[positive[i + 1]];
                if (next_off != off + sz) {
                    contiguous = 0;
                }
            }
        }

        size_t leading_gap = first_off - table_end;
        size_t trailing = n - payload_end;

        int trailing_ok = 0;
        if (trailing == 0) {
            trailing_ok = 1;
        } else if (trailing <= 0x40 && all_zero(blob, payload_end, n)) {
            trailing_ok = 1;
        } else if (trailing == 6 && blob[payload_end] < 0x40 &&
                   (blob[payload_end + 5] == 0 || blob[payload_end + 5] == 1)) {
            trailing_ok = 1;
        }

        int packed = first_off >= table_end && leading_gap <= 0x40 &&
                     all_zero(blob, table_end, first_off) && contiguous && trailing_ok;
        int tightly_packed = packed && hits >= 1 && first_positive_meaningful;
        int fully_tiled = packed && empty_slots_follow_data(offs, sizes, count, table_end);
        if (!tightly_packed && !fully_tiled) {
            return 0;
        }
    }

    memset(out, 0, sizeof(*out));
    out->kind = LAYOUT_PAIRTABLE;
    out->big_endian = big;
    out->count = count;
    out->offs = offs;
    out->sizes = sizes;
    out->positive = positive;
    out->positive_count = positive_count;
    out->table_end = table_end;
    return 1;
}

static int read_relpair_block(const unsigned char *blob, size_t len, size_t start, size_t block_end,
                              arena *a, sub_block *out, int big) {
    if (block_end > len || start + 12 > block_end) {
        return 0;
    }

    uint32_t declared_count = word_at(blob, start, big);
    uint32_t payload_base_rel = word_at(blob, start + 4, big);

    if (declared_count <= 1 || declared_count > NESTED_MAX_COUNT) {
        return 0;
    }
    if (payload_base_rel < 12 || start + payload_base_rel > block_end) {
        return 0;
    }

    size_t table_bytes = payload_base_rel - 12;
    if (table_bytes == 0 || table_bytes % 8 != 0) {
        return 0;
    }
    size_t entry_count = table_bytes / 8;
    if (entry_count == 0) {
        return 0;
    }
    if (declared_count != entry_count && declared_count != entry_count + 1) {
        return 0;
    }

    size_t *offs = (size_t *)arena_alloc(a, sizeof(size_t) * entry_count);
    size_t *sizes = (size_t *)arena_alloc(a, sizeof(size_t) * entry_count);
    size_t *rels = (size_t *)arena_alloc(a, sizeof(size_t) * entry_count);
    if (offs == NULL || sizes == NULL || rels == NULL) {
        return 0;
    }

    size_t payload_base_abs = start + payload_base_rel;
    size_t positive = 0;
    int hits = 0;
    int checked = 0;

    for (size_t idx = 0; idx < entry_count; idx++) {
        size_t ent_off = start + 8 + idx * 8;
        size_t rel = word_at(blob, ent_off, big);
        size_t sz = word_at(blob, ent_off + 4, big);
        size_t abs_off = payload_base_abs + rel;
        if (sz > 0) {
            if (abs_off < payload_base_abs || abs_off + sz > block_end) {
                return 0;
            }
            positive++;
            if (checked < 6) {
                if (nested_payload_looks_meaningful(blob + abs_off, sz, 0)) {
                    hits++;
                }
                checked++;
            }
        }
        rels[idx] = rel;
        sizes[idx] = sz;
        offs[idx] = abs_off;
    }

    if (positive == 0) {
        return 0;
    }
    if (checked > 0 && hits == 0) {
        return 0;
    }

    memset(out, 0, sizeof(*out));
    out->kind = BLOCK_RELPAIR;
    out->big_endian = big;
    out->start = start;
    out->end = block_end;
    out->count = entry_count;
    out->offs = offs;
    out->sizes = sizes;
    out->rels = rels;
    out->declared_count = declared_count;
    out->entry_count = entry_count;
    out->payload_base_rel = payload_base_rel;
    out->payload_base_abs = payload_base_abs;
    out->reserved_start = start + 8 + entry_count * 8;
    return 1;
}

static int read_relpairtable_block(const unsigned char *blob, size_t len, size_t start,
                                   size_t block_end, arena *a, sub_block *out, int big) {
    if (block_end > len || start + 12 > block_end) {
        return 0;
    }

    uint32_t count = word_at(blob, start, big);
    if (count == 0 || count > NESTED_MAX_COUNT) {
        return 0;
    }
    size_t table_end = start + 4 + (size_t)count * 8;
    if (table_end > block_end) {
        return 0;
    }

    size_t *offs = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    size_t *sizes = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    size_t *rels = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    if (offs == NULL || sizes == NULL || rels == NULL) {
        return 0;
    }

    int64_t last_abs = -1;
    size_t positive = 0;
    int hits = 0;
    int checked = 0;
    size_t max_payload_end = table_end;

    for (uint32_t idx = 0; idx < count; idx++) {
        size_t ent_off = start + 4 + (size_t)idx * 8;
        size_t rel = word_at(blob, ent_off, big);
        size_t sz = word_at(blob, ent_off + 4, big);
        size_t abs_off = start + rel;
        if (sz > 0) {
            if (abs_off < table_end || abs_off + sz > block_end) {
                return 0;
            }
            if ((int64_t)abs_off < last_abs) {
                return 0;
            }
            last_abs = (int64_t)abs_off;
            positive++;
            if (abs_off + sz > max_payload_end) {
                max_payload_end = abs_off + sz;
            }
            if (checked < 6) {
                if (nested_payload_looks_meaningful(blob + abs_off, sz, 0)) {
                    hits++;
                }
                checked++;
            }
        }
        rels[idx] = rel;
        sizes[idx] = sz;
        offs[idx] = abs_off;
    }

    size_t trailing = block_end - max_payload_end;
    if (positive == 0) {
        return 0;
    }
    if (trailing > 0x40) {
        return 0;
    }
    if (checked > 0 && hits == 0 && count > 1) {
        return 0;
    }

    memset(out, 0, sizeof(*out));
    out->kind = BLOCK_RELPAIRTABLE;
    out->big_endian = big;
    out->start = start;
    out->end = block_end;
    out->count = count;
    out->offs = offs;
    out->sizes = sizes;
    out->rels = rels;
    out->table_end = table_end;
    return 1;
}

static int read_simple_block(const unsigned char *blob, size_t len, size_t start, size_t block_end,
                             arena *a, sub_block *out, int big) {
    if (start + 8 > len || block_end > len || start >= block_end) {
        return 0;
    }

    uint32_t count = word_at(blob, start, big);
    uint32_t cap = NESTED_MAX_COUNT < 4096 ? NESTED_MAX_COUNT : 4096;
    if (count == 0 || count > cap) {
        return 0;
    }

    size_t *offs = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    size_t *sizes = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    if (offs == NULL || sizes == NULL) {
        return 0;
    }

    size_t cursor = start + 4;
    size_t positive = 0;
    int hits = 0;
    int checked = 0;

    for (uint32_t idx = 0; idx < count; idx++) {
        if (cursor + 4 > block_end) {
            return 0;
        }
        size_t sz = word_at(blob, cursor, big);
        cursor += 4;
        if (cursor + sz > block_end) {
            return 0;
        }
        offs[idx] = cursor;
        sizes[idx] = sz;
        if (sz > 0) {
            positive++;
            if (checked < 6) {
                if (nested_payload_looks_meaningful(blob + cursor, sz, 0)) {
                    hits++;
                }
                checked++;
            }
        }
        cursor += sz;
    }

    size_t trailing = block_end - cursor;
    if (positive == 0 || trailing > 0x40) {
        return 0;
    }
    if (checked > 0 && hits == 0) {
        return 0;
    }

    memset(out, 0, sizeof(*out));
    out->kind = BLOCK_SIMPLE;
    out->big_endian = big;
    out->start = start;
    out->end = cursor;
    out->count = count;
    out->offs = offs;
    out->sizes = sizes;
    return 1;
}

static int read_multiblock_layout(const unsigned char *blob, size_t n, arena *a,
                                  sub_layout *out, int big) {
    if (n < 0x20) {
        return 0;
    }

    uint32_t block_count = word_at(blob, 0, big);
    uint32_t primary_block_off = word_at(blob, 4, big);
    uint32_t cap = NESTED_MAX_COUNT < 4096 ? NESTED_MAX_COUNT : 4096;
    if (block_count == 0 || block_count > cap) {
        return 0;
    }
    if (primary_block_off < 0x10 || primary_block_off > n) {
        return 0;
    }

    size_t dynamic_header_end = 8 + (size_t)block_count * 4;
    if (dynamic_header_end > primary_block_off) {
        return 0;
    }

    size_t *later_offsets = (size_t *)arena_alloc(a, sizeof(size_t) * block_count);
    if (later_offsets == NULL) {
        return 0;
    }
    for (uint32_t idx = 0; idx < block_count; idx++) {
        size_t off = word_at(blob, 8 + (size_t)idx * 4, big);
        if (off <= primary_block_off || off >= n) {
            return 0;
        }
        later_offsets[idx] = off;
        if (idx > 0 && later_offsets[idx] < later_offsets[idx - 1]) {
            return 0;
        }
    }

    int has_tail_field = dynamic_header_end + 4 <= primary_block_off;
    size_t tail_field_off = has_tail_field ? dynamic_header_end : 0;
    size_t last_block_span = 0;
    int has_span = 0;
    if (has_tail_field) {
        last_block_span = word_at(blob, tail_field_off, big);
        if (last_block_span > 0 && later_offsets[block_count - 1] + last_block_span <= n) {
            has_span = 1;
        }
    }

    size_t *cand_ends = (size_t *)arena_alloc(a, sizeof(size_t) * (block_count + 1));
    if (cand_ends == NULL) {
        return 0;
    }
    size_t cand_count = 0;
    for (uint32_t idx = 0; idx < block_count; idx++) {
        if (later_offsets[idx] > primary_block_off) {
            cand_ends[cand_count++] = later_offsets[idx];
        }
    }
    cand_ends[cand_count++] = n;
    qsort(cand_ends, cand_count, sizeof(size_t), cmp_size);

    sub_block primary;
    int have_primary = 0;
    for (size_t i = 0; i < cand_count; i++) {
        if (i > 0 && cand_ends[i] == cand_ends[i - 1]) {
            continue;
        }
        if (read_relpairtable_block(blob, n, primary_block_off, cand_ends[i], a, &primary, big)) {
            have_primary = 1;
            break;
        }
    }

    if (!have_primary) {
        if (read_relpair_block(blob, n, primary_block_off, later_offsets[0], a, &primary, big)) {
            have_primary = 1;
        }
    }

    if (!have_primary) {
        sub_block trailing_primary;
        if (read_relpairtable_block(blob, n, primary_block_off, n, a, &trailing_primary, big)) {
            size_t trailer_start = later_offsets[0];
            if (trailer_start < n && all_zero(blob, trailer_start, n)) {
                memset(out, 0, sizeof(*out));
                out->kind = LAYOUT_MULTIBLOCK;
                out->outer_count = block_count;
                out->primary_block_off = primary_block_off;
                out->primary = trailing_primary;
                out->later = NULL;
                out->later_count = 0;
                out->later_block_offsets = later_offsets;
                out->later_offset_count = block_count;
                out->has_tail_field = has_tail_field;
                out->tail_field_off = tail_field_off;
                out->has_last_block_span = has_span;
                out->last_block_span = last_block_span;
                return 1;
            }
        }
        return 0;
    }

    size_t *active = (size_t *)arena_alloc(a, sizeof(size_t) * block_count);
    if (active == NULL) {
        return 0;
    }
    size_t active_count = 0;
    for (uint32_t idx = 0; idx < block_count; idx++) {
        if (later_offsets[idx] >= primary.end) {
            active[active_count++] = later_offsets[idx];
        }
    }

    sub_block *later = (sub_block *)arena_alloc(a, sizeof(sub_block) * (active_count + 1));
    if (later == NULL && active_count > 0) {
        return 0;
    }
    size_t later_count = 0;

    for (size_t idx = 0; idx < active_count; idx++) {
        size_t start = active[idx];
        size_t end;
        if (idx + 1 < active_count) {
            end = active[idx + 1];
        } else if (has_span) {
            end = start + last_block_span;
            if (end > n) {
                end = n;
            }
        } else {
            end = n;
        }
        if (end <= start) {
            return 0;
        }

        sub_block block;
        if (read_relpairtable_block(blob, n, start, end, a, &block, big) ||
            read_relpair_block(blob, n, start, end, a, &block, big) ||
            read_simple_block(blob, n, start, end, a, &block, big)) {
            later[later_count++] = block;
            continue;
        }

        if (end <= n && all_zero(blob, start, end)) {
            continue;
        }

        memset(&block, 0, sizeof(block));
        block.kind = BLOCK_RAW;
        block.start = start;
        block.end = end;
        block.count = 0;
        later[later_count++] = block;
    }

    memset(out, 0, sizeof(*out));
    out->kind = LAYOUT_MULTIBLOCK;
    out->big_endian = big;
    out->outer_count = block_count;
    out->primary_block_off = primary_block_off;
    out->primary = primary;
    out->later = later;
    out->later_count = later_count;
    out->later_block_offsets = later_offsets;
    out->later_offset_count = block_count;
    out->has_tail_field = has_tail_field;
    out->tail_field_off = tail_field_off;
    out->has_last_block_span = has_span;
    out->last_block_span = last_block_span;
    return 1;
}

static int read_wrapper_pair_layout(const unsigned char *blob, size_t n, arena *a,
                                    sub_layout *out, int big) {
    if (n < 16) {
        return 0;
    }

    size_t max_pairs = 512;
    size_t *offs = (size_t *)arena_alloc(a, sizeof(size_t) * max_pairs * 2);
    size_t *sizes = (size_t *)arena_alloc(a, sizeof(size_t) * max_pairs * 2);
    size_t *cursors = (size_t *)arena_alloc(a, sizeof(size_t) * max_pairs);
    if (offs == NULL || sizes == NULL || cursors == NULL) {
        return 0;
    }

    size_t cursor = 0;
    size_t pair_count = 0;
    size_t entry_count = 0;

    for (size_t pair_idx = 0; pair_idx < max_pairs; pair_idx++) {
        if (cursor + 16 > n) {
            break;
        }
        size_t wbh_off = word_at(blob, cursor, big);
        size_t wbh_size = word_at(blob, cursor + 4, big);
        size_t wbd_off = word_at(blob, cursor + 8, big);
        size_t wbd_size = word_at(blob, cursor + 12, big);

        if (wbh_off < 0x10 || wbh_size == 0 || wbd_off < 0x10 || wbd_size == 0) {
            break;
        }

        size_t wbh_abs = cursor + wbh_off;
        size_t wbd_abs = cursor + wbd_off;
        size_t wbh_end = wbh_abs + wbh_size;
        size_t wbd_end = wbd_abs + wbd_size;

        if (wbh_abs < cursor + 16 || wbh_end > n || wbd_abs < cursor + 16 || wbd_end > n) {
            break;
        }
        if (wbh_end > wbd_abs) {
            break;
        }
        const char *hbw = big ? "WBH_0000" : "_HBW0000";
        const char *dbw = big ? "WBD_0000" : "_DBW0000";
        if (wbh_abs + 8 > n || memcmp(blob + wbh_abs, hbw, 8) != 0) {
            break;
        }
        if (wbd_abs + 8 > n || memcmp(blob + wbd_abs, dbw, 8) != 0) {
            break;
        }

        cursors[pair_count] = cursor;
        offs[entry_count] = wbh_abs;
        sizes[entry_count] = wbh_size;
        entry_count++;
        offs[entry_count] = wbd_abs;
        sizes[entry_count] = wbd_size;
        entry_count++;
        pair_count++;

        size_t next_cursor = wbh_end > wbd_end ? wbh_end : wbd_end;
        if (next_cursor <= cursor) {
            break;
        }
        cursor = next_cursor;
    }

    if (pair_count == 0) {
        return 0;
    }

    memset(out, 0, sizeof(*out));
    out->kind = LAYOUT_WRAPPER_PAIRS;
    out->big_endian = big;
    out->pair_count = pair_count;
    out->pair_cursor = cursors;
    out->wp_off = offs;
    out->wp_size = sizes;
    out->wp_count = entry_count;
    out->count = entry_count;
    out->wrapper_end = cursor;
    return 1;
}

static int read_offsets_layout(const unsigned char *blob, size_t len, arena *a,
                               sub_layout *out, int big) {
    uint32_t count = 0;
    size_t *offsets = NULL;
    size_t table_end = 0;
    if (!read_subcontainer_toc(blob, len, a, &count, &offsets, &table_end, big)) {
        return 0;
    }
    if (!is_real_subcontainer(blob, len, offsets, count, table_end) ||
        offsets_point_inside_known_spans(blob, len, offsets, count, table_end)) {
        return 0;
    }
    size_t *sorted = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    if (sorted == NULL) {
        return 0;
    }
    size_t kept = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (offsets[i] >= table_end && offsets[i] < len) {
            sorted[kept++] = offsets[i];
        }
    }
    qsort(sorted, kept, sizeof(size_t), cmp_size);
    size_t unique_count = 0;
    for (size_t i = 0; i < kept; i++) {
        if (i > 0 && sorted[i] == sorted[i - 1]) {
            continue;
        }
        sorted[unique_count++] = sorted[i];
    }
    if (unique_count < 2) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->kind = LAYOUT_OFFSETS;
    out->big_endian = big;
    out->count = count;
    out->offs = offsets;
    out->table_end = table_end;
    out->unique_offsets = sorted;
    out->unique_count = unique_count;
    return 1;
}

static size_t align16(size_t value) {
    return (value + 15) / 16 * 16;
}

static int all_ff(const unsigned char *blob, size_t start, size_t end) {
    for (size_t i = start; i < end; i++) {
        if (blob[i] != 0xFF) {
            return 0;
        }
    }
    return 1;
}

static int read_tiled_sizes_layout(const unsigned char *blob, size_t n, arena *a,
                                   sub_layout *out, int big) {
    if (n < 32) {
        return 0;
    }
    uint32_t count = word_at(blob, 0, big);
    if (count < 1 || count > NESTED_MAX_COUNT) {
        return 0;
    }
    size_t table_end = 4 + (size_t)count * 4;
    size_t data_start = align16(table_end);
    if (data_start >= n || !all_zero(blob, table_end, data_start)) {
        return 0;
    }
    size_t *sizes = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    if (sizes == NULL) {
        return 0;
    }
    uint64_t total = 0;
    size_t nonzero = 0;
    int aligned = 1;
    for (uint32_t i = 0; i < count; i++) {
        sizes[i] = word_at(blob, 4 + (size_t)i * 4, big);
        total += sizes[i];
        if (sizes[i] != 0) {
            nonzero++;
        }
        if (sizes[i] % 16 != 0) {
            aligned = 0;
        }
    }
    if (nonzero == 0 || (uint64_t)data_start + total > (uint64_t)n ||
        align16(data_start + (size_t)total) != n ||
        (!all_zero(blob, data_start + (size_t)total, n) && !all_ff(blob, data_start + (size_t)total, n))) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->kind = LAYOUT_SEQUENTIAL;
    out->big_endian = big;
    out->count = count;
    out->seq_count = count;
    out->seq_sizes = sizes;
    out->table_end = table_end;
    out->data_start = data_start;
    out->child_align = aligned ? 16 : 0;
    return 1;
}

static int read_tiled_offsets_layout(const unsigned char *blob, size_t n, arena *a,
                                     sub_layout *out, int big) {
    if (n < 32) {
        return 0;
    }
    uint32_t count = word_at(blob, 0, big);
    if (count < 1 || count > NESTED_MAX_COUNT) {
        return 0;
    }
    size_t table_end = 4 + (size_t)count * 4;
    size_t data_start = align16(table_end);
    if (data_start >= n) {
        return 0;
    }
    int ff_pad = data_start > table_end && all_ff(blob, table_end, data_start);
    if (!ff_pad && !all_zero(blob, table_end, data_start)) {
        return 0;
    }
    size_t *offsets = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    size_t *unique = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    if (offsets == NULL || unique == NULL) {
        return 0;
    }
    size_t unique_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        offsets[i] = word_at(blob, 4 + (size_t)i * 4, big);
        if (offsets[i] % 16 != 0 || offsets[i] >= n) {
            return 0;
        }
        if (i == 0 ? offsets[i] != data_start : offsets[i] < offsets[i - 1]) {
            return 0;
        }
        if (i > 0 && offsets[i] == offsets[i - 1]) {
            if (!ff_pad) {
                return 0;
            }
            continue;
        }
        unique[unique_count++] = offsets[i];
    }
    if (!ff_pad) {
        int meaningful = 0;
        for (size_t i = 0; i < unique_count && !meaningful; i++) {
            size_t end = i + 1 < unique_count ? unique[i + 1] : n;
            sub_layout child;
            meaningful = nested_payload_looks_meaningful(blob + unique[i], end - unique[i], 0) ||
                         read_tiled_sizes_layout(blob + unique[i], end - unique[i], a, &child, big);
        }
        if (!meaningful) {
            return 0;
        }
    }
    memset(out, 0, sizeof(*out));
    out->kind = LAYOUT_OFFSETS;
    out->big_endian = big;
    out->count = count;
    out->offs = offsets;
    out->table_end = table_end;
    out->unique_offsets = unique;
    out->unique_count = unique_count;
    out->child_align = 16;
    return 1;
}

static int read_layouts(const unsigned char *blob, size_t len, arena *a,
                        sub_layout *out, int big) {
    if (read_wrapper_pair_layout(blob, len, a, out, big)) {
        return 1;
    }
    if (read_multiblock_layout(blob, len, a, out, big)) {
        return 1;
    }
    if (read_pairtable_layout(blob, len, a, out, big)) {
        return 1;
    }
    if (read_tiled_sizes_layout(blob, len, a, out, big)) {
        return 1;
    }
    if (read_offsets_layout(blob, len, a, out, big)) {
        return 1;
    }
    if (read_sequential_layout(blob, len, a, out, big)) {
        return 1;
    }
    return read_tiled_offsets_layout(blob, len, a, out, big);
}

int nested_read_universal_layout(const unsigned char *blob, size_t len, arena *a, sub_layout *out) {
    if (codec_big_endian() && read_layouts(blob, len, a, out, 1)) {
        return 1;
    }
    return read_layouts(blob, len, a, out, 0);
}

size_t nested_layout_payload_ranges(const unsigned char *blob, size_t len, const sub_layout *layout,
                                    arena *a, size_t **offs_out, size_t **sizes_out) {
    size_t cap = layout->count + layout->seq_count + layout->unique_count + 8;
    if (layout->kind == LAYOUT_MULTIBLOCK) {
        cap = layout->primary.count + 8;
        for (size_t i = 0; i < layout->later_count; i++) {
            cap += layout->later[i].count;
        }
    }

    size_t *offs = (size_t *)arena_alloc(a, sizeof(size_t) * (cap + 1));
    size_t *sizes = (size_t *)arena_alloc(a, sizeof(size_t) * (cap + 1));
    if (offs == NULL || sizes == NULL) {
        *offs_out = NULL;
        *sizes_out = NULL;
        return 0;
    }
    size_t count = 0;

    if (layout->kind == LAYOUT_MULTIBLOCK) {
        for (size_t i = 0; i < layout->primary.count; i++) {
            size_t off = layout->primary.offs[i];
            size_t sz = layout->primary.sizes[i];
            if (sz > 0 && off + sz <= len) {
                offs[count] = off;
                sizes[count] = sz;
                count++;
            }
        }
        for (size_t b = 0; b < layout->later_count; b++) {
            for (size_t i = 0; i < layout->later[b].count; i++) {
                size_t off = layout->later[b].offs[i];
                size_t sz = layout->later[b].sizes[i];
                if (sz > 0 && off + sz <= len) {
                    offs[count] = off;
                    sizes[count] = sz;
                    count++;
                }
            }
        }
    } else if (layout->kind == LAYOUT_WRAPPER_PAIRS) {
        for (size_t i = 0; i < layout->wp_count; i++) {
            size_t off = layout->wp_off[i];
            size_t sz = layout->wp_size[i];
            if (sz > 0 && off + sz <= len) {
                offs[count] = off;
                sizes[count] = sz;
                count++;
            }
        }
    } else if (layout->kind == LAYOUT_OFFSETS) {
        for (size_t i = 0; i < layout->unique_count; i++) {
            size_t start = layout->unique_offsets[i];
            size_t end = (i + 1 < layout->unique_count) ? layout->unique_offsets[i + 1] : len;
            if (end > start && end <= len) {
                offs[count] = start;
                sizes[count] = end - start;
                count++;
            }
        }
    } else if (layout->kind == LAYOUT_SEQUENTIAL) {
        size_t cur = layout->data_start;
        for (size_t i = 0; i < layout->seq_count; i++) {
            size_t sz = layout->seq_sizes[i];
            if (sz == 0) {
                continue;
            }
            if (cur + sz > len) {
                break;
            }
            offs[count] = cur;
            sizes[count] = sz;
            count++;
            cur += sz;
        }
    } else {
        for (size_t i = 0; i < layout->count; i++) {
            size_t off = layout->offs[i];
            size_t sz = layout->sizes[i];
            if (sz > 0 && off + sz <= len) {
                offs[count] = off;
                sizes[count] = sz;
                count++;
            }
        }
    }

    *offs_out = offs;
    *sizes_out = sizes;
    return count;
}

static size_t estimate_layout_payload_end(const unsigned char *blob, size_t len,
                                          const sub_layout *layout, arena *a) {
    size_t *offs = NULL;
    size_t *sizes = NULL;
    size_t count = nested_layout_payload_ranges(blob, len, layout, a, &offs, &sizes);
    if (count == 0) {
        return len;
    }
    size_t best = 0;
    for (size_t i = 0; i < count; i++) {
        if (offs[i] + sizes[i] > best) {
            best = offs[i] + sizes[i];
        }
    }
    return best;
}

int nested_read_mdlk_layout(const unsigned char *blob, size_t n, arena *a, mdlk_layout *out) {
    if (n < 16) {
        return 0;
    }
    int big;
    if (memcmp(blob, "MDLK", 4) == 0) {
        big = 0;
    } else if (memcmp(blob, "KLDM", 4) == 0) {
        big = 1;
    } else {
        return 0;
    }

    uint16_t count = big ? (uint16_t)((blob[8] << 8) | blob[9]) : codec_u16(blob, 8);
    if (count == 0) {
        return 0;
    }

    res_entry *entries = (res_entry *)arena_alloc(a, sizeof(res_entry) * count);
    if (entries == NULL) {
        return 0;
    }

    size_t pos = 16;
    for (uint16_t idx = 0; idx < count; idx++) {
        if (pos + 4 > n) {
            return 0;
        }
        size_t size = 0;
        const char *ext = NULL;
        if (memcmp(blob + pos, big ? "G1M_" : "_M1G", 4) == 0) {
            if (pos + 12 > n) {
                return 0;
            }
            size = word_at(blob, pos + 8, big);
            ext = ".g1m";
        } else if (memcmp(blob + pos, big ? "G1CO" : "OC1G", 4) == 0) {
            if (pos + 0x10 > n) {
                return 0;
            }
            size = word_at(blob, pos + 0x0C, big);
            ext = ".g1c";
        } else {
            return 0;
        }
        if (size == 0 || pos + size > n) {
            return 0;
        }
        entries[idx].offset = pos;
        entries[idx].size = size;
        entries[idx].ext = ext;
        pos += size;
    }

    out->count = count;
    out->entries = entries;
    out->payload_end = pos;
    return 1;
}

static int kshl_endian(const unsigned char *blob, size_t n) {
    if (n < 0xB8) {
        return -1;
    }
    if (memcmp(blob, "LHSK", 4) == 0) {
        return 0;
    }
    if (memcmp(blob, "KSHL", 4) == 0) {
        return 1;
    }
    return -1;
}

int nested_looks_like_kshl(const unsigned char *blob, size_t n) {
    int big = kshl_endian(blob, n);
    if (big < 0) {
        return 0;
    }
    size_t size = word_at(blob, 0x08, big);
    if (size < 0xB8 || size > n) {
        return 0;
    }
    size_t payload_start = word_at(blob, 0xB0, big);
    size_t payload_size = word_at(blob, 0xB4, big);
    if (payload_start < 0xB8 || payload_size == 0) {
        return 0;
    }
    return payload_start + payload_size == size;
}

int nested_read_kshl_layout(const unsigned char *blob, size_t n, arena *a, kshl_layout *out) {
    if (!nested_looks_like_kshl(blob, n)) {
        return 0;
    }

    int big = kshl_endian(blob, n);
    size_t size = word_at(blob, 0x08, big);
    size_t payload_start = word_at(blob, 0xB0, big);
    size_t payload_size = word_at(blob, 0xB4, big);
    size_t payload_end = payload_start + payload_size;

    if (payload_start < 0xB8 || payload_end != size || payload_end > n) {
        return 0;
    }

    size_t cap = 64;
    size_t *starts = (size_t *)malloc(sizeof(size_t) * cap);
    if (starts == NULL) {
        return 0;
    }
    size_t start_count = 0;

    size_t anchor = payload_start + 8;
    for (;;) {
        size_t ctab = nested_find_magic(blob, payload_end, anchor, "CTAB");
        if (ctab == (size_t)-1 || ctab < 8) {
            break;
        }
        anchor = ctab + 1;
        size_t abs_off = ctab - 8;
        if (abs_off < payload_start || abs_off + 12 > payload_end) {
            continue;
        }
        if (codec_dx9_ext_at(blob, n, abs_off) == NULL) {
            continue;
        }
        if (start_count == cap) {
            cap *= 2;
            size_t *grown = (size_t *)realloc(starts, sizeof(size_t) * cap);
            if (grown == NULL) {
                free(starts);
                return 0;
            }
            starts = grown;
        }
        starts[start_count++] = abs_off;
    }

    if (start_count == 0) {
        free(starts);
        return 0;
    }

    res_entry *entries = (res_entry *)arena_alloc(a, sizeof(res_entry) * start_count);
    if (entries == NULL) {
        free(starts);
        return 0;
    }

    for (size_t idx = 0; idx < start_count; idx++) {
        size_t abs_off = starts[idx];
        size_t next_abs = (idx + 1 < start_count) ? starts[idx + 1] : payload_end;
        if (next_abs <= abs_off) {
            free(starts);
            return 0;
        }
        const char *ext = codec_dx9_ext_at(blob, n, abs_off);
        entries[idx].offset = abs_off;
        entries[idx].size = next_abs - abs_off;
        entries[idx].ext = ext == NULL ? ".bin" : ext;
    }
    free(starts);

    out->size = size;
    out->payload_start = payload_start;
    out->payload_size = payload_size;
    out->payload_end = payload_end;
    out->count = start_count;
    out->entries = entries;
    return 1;
}

int nested_looks_like_embedded_mdlk(const unsigned char *blob, size_t n) {
    if (n >= 16 && memcmp(blob, "MDLK", 4) == 0) {
        return 0;
    }

    size_t search = 0;
    size_t previous_end = 0;
    for (;;) {
        size_t off = nested_find_magic(blob, n, search, "MDLK");
        if (off == (size_t)-1) {
            break;
        }
        scratch_node *node = scratch_get();
        mdlk_layout layout;
        int ok = node != NULL && nested_read_mdlk_layout(blob + off, n - off, &node->a, &layout);
        size_t payload_end = ok ? layout.payload_end : 0;
        scratch_put(node);

        if (!ok || payload_end <= 16 || off + payload_end > n || off < previous_end) {
            search = off + 1;
            continue;
        }
        return 1;
    }
    return 0;
}

const char *nested_resolve_payload_ext(const unsigned char *chunk, size_t len) {
    const char *inner = codec_detect_ext(chunk, len);

    if (strcmp(inner, ".ini") == 0 || strcmp(inner, ".txt") == 0) {
        size_t head = len < 64 ? len : 64;
        for (size_t i = 0; i < head; i++) {
            if (chunk[i] == 0) {
                return ".bin";
            }
        }
    }
    if (strcmp(inner, ".bin") != 0) {
        return inner;
    }

    scratch_node *node = scratch_get();
    if (node == NULL) {
        return ".bin";
    }
    sub_layout layout;
    int has_layout = nested_read_universal_layout(chunk, len, &node->a, &layout);
    int split = codec_looks_like_pairtable(chunk, len, &node->a) ||
                codec_looks_like_classic_split(chunk, len, &node->a);
    scratch_put(node);

    if (has_layout || split) {
        return ".bin";
    }
    if (nested_looks_like_embedded_mdlk(chunk, len)) {
        return ".mdlk";
    }
    return ".bin";
}

int nested_should_recurse(const char *ext, const unsigned char *chunk, size_t len) {
    if (ext != NULL) {
        if (strcmp(ext, ".bin") == 0 || strcmp(ext, ".kvs") == 0 ||
            strcmp(ext, ".mdlk") == 0 || strcmp(ext, ".KSHL") == 0 ||
            strcmp(ext, ".colk") == 0 || strcmp(ext, ".zp1") == 0 ||
            strcmp(ext, ".cstb") == 0) {
            return 1;
        }
    }
    if (len >= 4 && (memcmp(chunk, "MDLK", 4) == 0 || memcmp(chunk, "LHSK", 4) == 0 ||
                     memcmp(chunk, "BTSC", 4) == 0 ||
                     memcmp(chunk, "COLK", 4) == 0 || memcmp(chunk, "KOVS", 4) == 0 ||
                     memcmp(chunk, "zp1", 3) == 0)) {
        return 1;
    }

    scratch_node *node = scratch_get();
    if (node == NULL) {
        return 0;
    }
    int hit = codec_looks_like_pairtable(chunk, len, &node->a) ||
              codec_looks_like_classic_split(chunk, len, &node->a);
    if (!hit) {
        sub_layout layout;
        hit = nested_read_universal_layout(chunk, len, &node->a, &layout);
    }
    scratch_put(node);
    return hit;
}

static char *derive_out_dir(const char *path) {
    const char *slash = strrchr(path, '\\');
    const char *fname = slash == NULL ? path : slash + 1;
    const char *dot = strrchr(fname, '.');

    size_t dir_len = slash == NULL ? 0 : (size_t)(slash - path);
    size_t stem_len = dot == NULL ? strlen(fname) : (size_t)(dot - fname);

    size_t total = dir_len + 1 + stem_len + 1;
    char *out = (char *)malloc(total);
    if (out == NULL) {
        return NULL;
    }
    if (dir_len > 0) {
        memcpy(out, path, dir_len);
        out[dir_len] = '\\';
        memcpy(out + dir_len + 1, fname, stem_len);
        out[dir_len + 1 + stem_len] = 0;
    } else {
        memcpy(out, fname, stem_len);
        out[stem_len] = 0;
    }
    return out;
}

static int write_child(job_ctx *job, const char *out_dir, const char *name,
                       const unsigned char *chunk, size_t len, int recurse, int depth,
                       int64_t *written) {
    char *out_path = path_join(out_dir, name);
    if (out_path == NULL) {
        return 0;
    }
    if (!file_write_prepared(out_path, chunk, len)) {
        free(out_path);
        return 0;
    }
    if (written != NULL) {
        (*written)++;
    }
    if (recurse) {
        nested_unpack_resource(job, out_path, chunk, len, depth + 1, written);
    }
    free(out_path);
    return 1;
}

int nested_colk_endian(const unsigned char *blob, size_t n) {
    if (n < 32) {
        return -1;
    }
    if (memcmp(blob, "COLK", 4) == 0) {
        size_t le = codec_u32(blob, 8);
        size_t be = word_at(blob, 8, 1);
        if (le >= 2 && le <= 8192) {
            return 0;
        }
        if (be >= 2 && be <= 8192) {
            return 1;
        }
        return -1;
    }
    if (memcmp(blob, "KLOC", 4) == 0) {
        return 1;
    }
    return -1;
}

int nested_looks_like_colk(const unsigned char *blob, size_t n) {
    int big = nested_colk_endian(blob, n);
    if (big < 0) {
        return 0;
    }
    size_t count = word_at(blob, 8, big);
    if (count < 2 || count > 8192) {
        return 0;
    }
    size_t table_end = 12 + count * 4;
    if (table_end + 4 > n) {
        return 0;
    }
    size_t previous = table_end;
    for (size_t i = 0; i < count; i++) {
        size_t off = word_at(blob, 12 + i * 4, big);
        if (off < table_end || off > n || off < previous || (off % 16) != 0) {
            return 0;
        }
        previous = off;
    }
    return 1;
}

size_t nested_colk_children(const unsigned char *blob, size_t n, size_t index,
                            size_t *start_out) {
    int big = nested_colk_endian(blob, n);
    if (big < 0) {
        return 0;
    }
    size_t count = word_at(blob, 8, big);
    if (index >= count) {
        return 0;
    }
    size_t start = word_at(blob, 12 + index * 4, big);
    size_t end = index + 1 < count ? word_at(blob, 12 + (index + 1) * 4, big) : n;
    if (end < start || end > n) {
        end = start;
    }
    if (start_out != NULL) {
        *start_out = start;
    }
    return end - start;
}

static int unpack_colk_blob(job_ctx *job, const unsigned char *blob, size_t n,
                            const char *out_dir, int depth, int64_t *written) {
    if (!nested_looks_like_colk(blob, n)) {
        return 0;
    }
    if (!path_make_dirs(out_dir)) {
        return 0;
    }
    size_t count = word_at(blob, 8, nested_colk_endian(blob, n));
    for (size_t i = 0; i < count; i++) {
        size_t start = 0;
        size_t size = nested_colk_children(blob, n, i, &start);
        char name[40];
        snprintf(name, sizeof(name), "%05zu%s", i,
                 size > 0 ? nested_resolve_payload_ext(blob + start, size) : ".bin");
        write_child(job, out_dir, name, blob + start, size, 0, depth, written);
    }
    return 1;
}

static int unpack_zp1_blob(job_ctx *job, const unsigned char *blob, size_t n,
                           const char *out_dir, int depth, int64_t *written) {
    if (!zp1_looks_like(blob, n)) {
        return 0;
    }
    buf plain;
    buf_init(&plain);
    err quiet;
    err_clear(&quiet);
    if (!zp1_decompress(blob, n, &plain, &quiet)) {
        buf_free(&plain);
        return 0;
    }
    if (!path_make_dirs(out_dir)) {
        buf_free(&plain);
        return 0;
    }
    const unsigned char *body = (const unsigned char *)plain.data;
    char name[40];
    snprintf(name, sizeof(name), "00000%s",
             plain.len > 0 ? nested_resolve_payload_ext(body, plain.len) : ".bin");
    write_child(job, out_dir, name, body, plain.len, 1, depth, written);
    buf_free(&plain);
    return 1;
}

size_t nested_kvs_bank_count(const unsigned char *blob, size_t n) {
    if (n < NESTED_KVS_BANK_HEADER + 32) {
        return 0;
    }
    int big = codec_big_endian();
    size_t count = word_at(blob, 0, big);
    if (count == 0 || count > NESTED_MAX_COUNT) {
        return 0;
    }
    unsigned char fill = blob[8];
    for (size_t i = 8; i < NESTED_KVS_BANK_HEADER; i++) {
        if (blob[i] != fill) {
            return 0;
        }
    }
    size_t pos = NESTED_KVS_BANK_HEADER;
    for (size_t i = 0; i < count; i++) {
        if (pos + 32 > n || memcmp(blob + pos, "KOVS", 4) != 0) {
            return 0;
        }
        size_t end = pos + 32 + (size_t)word_at(blob, pos + 4, big);
        size_t next = (end + NESTED_KVS_BANK_HEADER - 1) / NESTED_KVS_BANK_HEADER * NESTED_KVS_BANK_HEADER;
        if (end > n || next > n) {
            return 0;
        }
        for (size_t k = end; k < next; k++) {
            if (blob[k] != fill) {
                return 0;
            }
        }
        pos = next;
    }
    return pos == n ? count : 0;
}

static int unpack_kvs_bank_blob(job_ctx *job, const unsigned char *blob, size_t n,
                                const char *out_dir, int depth, int64_t *written) {
    size_t count = nested_kvs_bank_count(blob, n);
    if (count == 0 || !path_make_dirs(out_dir)) {
        return 0;
    }
    int big = codec_big_endian();
    size_t pos = NESTED_KVS_BANK_HEADER;
    for (size_t i = 0; i < count; i++) {
        size_t size = 32 + (size_t)word_at(blob, pos + 4, big);
        char name[40];
        snprintf(name, sizeof(name), "%05zu.kvs", i);
        write_child(job, out_dir, name, blob + pos, size, 0, depth, written);
        pos = (pos + size + NESTED_KVS_BANK_HEADER - 1) / NESTED_KVS_BANK_HEADER * NESTED_KVS_BANK_HEADER;
    }
    return 1;
}

static int unpack_cstb_blob(job_ctx *job, const unsigned char *blob, size_t n,
                            const char *out_dir, int depth, int64_t *written) {
    if (!btsc_looks_like(blob, n)) {
        return 0;
    }
    buf plain;
    buf_init(&plain);
    err quiet;
    err_clear(&quiet);
    if (!btsc_decompress(blob, n, &plain, &quiet)) {
        buf_free(&plain);
        return 0;
    }
    if (!path_make_dirs(out_dir)) {
        buf_free(&plain);
        return 0;
    }
    const unsigned char *body = (const unsigned char *)plain.data;
    char name[40];
    snprintf(name, sizeof(name), "000%s",
             plain.len > 0 ? nested_resolve_payload_ext(body, plain.len) : ".bin");
    write_child(job, out_dir, name, body, plain.len, 1, depth, written);
    buf_free(&plain);
    return 1;
}

static int unpack_kvs_blob(job_ctx *job, const unsigned char *blob, size_t n, const char *out_dir,
                           int64_t *written) {
    if (n < 32 || memcmp(blob, "KOVS", 4) != 0) {
        return 0;
    }
    if (!path_make_dirs(out_dir)) {
        return 0;
    }

    size_t pos = 0;
    int index = 0;
    for (;;) {
        if (pos + 32 > n) {
            break;
        }
        if (memcmp(blob + pos, "KOVS", 4) != 0) {
            int found = 0;
            size_t scan = pos;
            while (scan + 4 <= n) {
                if (memcmp(blob + scan, "KOVS", 4) == 0) {
                    pos = scan;
                    found = 1;
                    break;
                }
                scan += 4;
            }
            if (!found) {
                break;
            }
        }
        if (pos + 32 > n) {
            break;
        }

        size_t size = word_at(blob, pos + 4, codec_big_endian());
        if (size == 0) {
            break;
        }
        size_t data_start = pos + 32;
        size_t data_end = data_start + size;
        if (data_end > n) {
            break;
        }

        char name[32];
        snprintf(name, sizeof(name), "%05d.kvs", index);
        write_child(job, out_dir, name, blob + pos, data_end - pos, 0, 0, written);

        index++;
        pos = data_end;
        if (pos % 16 != 0) {
            pos = (pos + 15) & ~(size_t)0x0F;
        }
    }

    return index > 0;
}

static int unpack_mdlk_blob(job_ctx *job, const unsigned char *blob, size_t n, const char *out_dir,
                            int64_t *written) {
    scratch_node *node = scratch_get();
    mdlk_layout layout;
    if (node == NULL || !nested_read_mdlk_layout(blob, n, &node->a, &layout)) {
        scratch_put(node);
        return 0;
    }
    if (!path_make_dirs(out_dir)) {
        scratch_put(node);
        return 0;
    }

    for (size_t idx = 0; idx < layout.count; idx++) {
        char name[32];
        snprintf(name, sizeof(name), "%03zu%s", idx, layout.entries[idx].ext);
        write_child(job, out_dir, name, blob + layout.entries[idx].offset,
                    layout.entries[idx].size, 0, 0, written);
    }
    scratch_put(node);
    return 1;
}

static int unpack_kshl_blob(job_ctx *job, const unsigned char *blob, size_t n, const char *out_dir,
                            int64_t *written) {
    scratch_node *node = scratch_get();
    kshl_layout layout;
    if (node == NULL || !nested_read_kshl_layout(blob, n, &node->a, &layout)) {
        scratch_put(node);
        return 0;
    }
    if (!path_make_dirs(out_dir)) {
        scratch_put(node);
        return 0;
    }

    for (size_t idx = 0; idx < layout.count; idx++) {
        char name[32];
        snprintf(name, sizeof(name), "%03zu%s", idx, layout.entries[idx].ext);
        write_child(job, out_dir, name, blob + layout.entries[idx].offset,
                    layout.entries[idx].size, 0, 0, written);
    }
    scratch_put(node);
    return 1;
}

static int unpack_embedded_mdlk_blob(job_ctx *job, const unsigned char *blob, size_t n,
                                     const char *out_dir, int depth, int64_t *written) {
    if (n >= 16 && memcmp(blob, "MDLK", 4) == 0) {
        return 0;
    }

    size_t cap = 16;
    size_t *offsets = (size_t *)malloc(sizeof(size_t) * cap);
    size_t *sizes = (size_t *)malloc(sizeof(size_t) * cap);
    if (offsets == NULL || sizes == NULL) {
        free(offsets);
        free(sizes);
        return 0;
    }
    size_t found = 0;
    size_t search = 0;
    size_t previous_end = 0;

    for (;;) {
        size_t off = nested_find_magic(blob, n, search, "MDLK");
        if (off == (size_t)-1) {
            break;
        }
        scratch_node *node = scratch_get();
        mdlk_layout layout;
        int ok = node != NULL && nested_read_mdlk_layout(blob + off, n - off, &node->a, &layout);
        size_t payload_end = ok ? layout.payload_end : 0;
        scratch_put(node);

        if (!ok || payload_end <= 16 || off + payload_end > n || off < previous_end) {
            search = off + 1;
            continue;
        }

        if (found == cap) {
            cap *= 2;
            size_t *go = (size_t *)realloc(offsets, sizeof(size_t) * cap);
            size_t *gs = (size_t *)realloc(sizes, sizeof(size_t) * cap);
            if (go == NULL || gs == NULL) {
                free(go == NULL ? offsets : go);
                free(gs == NULL ? sizes : gs);
                return 0;
            }
            offsets = go;
            sizes = gs;
        }
        offsets[found] = off;
        sizes[found] = payload_end;
        found++;
        previous_end = off + payload_end;
        search = previous_end;
    }

    if (found == 0) {
        free(offsets);
        free(sizes);
        return 0;
    }

    if (!path_make_dirs(out_dir)) {
        free(offsets);
        free(sizes);
        return 0;
    }

    for (size_t idx = 0; idx < found; idx++) {
        char name[32];
        snprintf(name, sizeof(name), "%03zu.mdlk", idx);
        write_child(job, out_dir, name, blob + offsets[idx], sizes[idx], 1, depth, written);
    }

    free(offsets);
    free(sizes);
    return 1;
}

static int unpack_split_wrapper_blob(job_ctx *job, const char *path, const unsigned char *blob,
                                     size_t n, int depth, int64_t *written) {
    arena a;
    arena_init(&a);
    if (!codec_looks_like_pairtable(blob, n, &a)) {
        arena_free(&a);
        return 0;
    }
    size_t *offsets = NULL;
    size_t *sizes = NULL;
    uint32_t count = 0;
    if (!codec_read_pairtable_entries(blob, n, &a, &offsets, &sizes, &count)) {
        arena_free(&a);
        return 0;
    }

    char *out_dir = derive_out_dir(path);
    if (out_dir == NULL || !path_make_dirs(out_dir)) {
        free(out_dir);
        arena_free(&a);
        return 0;
    }

    for (uint32_t idx = 0; idx < count; idx++) {
        char name[32];
        snprintf(name, sizeof(name), "%03u.bin", idx);
        write_child(job, out_dir, name, blob + offsets[idx], sizes[idx], 1, depth, written);
    }

    free(out_dir);
    arena_free(&a);
    return 1;
}

static int unpack_classic_split_resource(job_ctx *job, const char *path, const unsigned char *blob,
                                         size_t n, int depth, int64_t *written) {
    arena a;
    arena_init(&a);
    if (!codec_looks_like_classic_split(blob, n, &a) && !codec_looks_like_stored_split(blob, n, &a)) {
        arena_free(&a);
        return 0;
    }

    buf merged;
    buf_init(&merged);
    const char *ext_hint = NULL;
    err e;
    err_clear(&e);
    if (!codec_decompress_classic_split(blob, n, &a, &merged, &ext_hint, &e)) {
        buf_free(&merged);
        arena_free(&a);
        return 0;
    }
    arena_free(&a);

    char *out_dir = derive_out_dir(path);
    if (out_dir == NULL || !path_make_dirs(out_dir)) {
        free(out_dir);
        buf_free(&merged);
        return 0;
    }

    const char *ext = codec_resolve_ext((const unsigned char *)merged.data, merged.len, ext_hint);
    char name[64];
    snprintf(name, sizeof(name), "000%s", ext);

    int recurse = strcmp(ext, ".bin") == 0 || strcmp(ext, ".kvs") == 0;
    write_child(job, out_dir, name, (const unsigned char *)merged.data, merged.len,
                recurse, depth, written);

    free(out_dir);
    buf_free(&merged);
    return 1;
}

static int try_unpack_subcontainer(job_ctx *job, const unsigned char *blob, size_t n,
                                   const char *out_dir, int depth, int64_t *written) {
    if (depth >= NESTED_MAX_DEPTH) {
        return 0;
    }

    arena a;
    arena_init(&a);

    if (codec_looks_like_pairtable(blob, n, &a)) {
        arena_free(&a);
        return 0;
    }

    sub_layout layout;
    if (!nested_read_universal_layout(blob, n, &a, &layout)) {
        arena_free(&a);
        return 0;
    }

    size_t *ranges_off = NULL;
    size_t *ranges_size = NULL;
    size_t range_count = nested_layout_payload_ranges(blob, n, &layout, &a, &ranges_off, &ranges_size);

    if (range_count == 1) {
        const unsigned char *payload = blob + ranges_off[0];
        size_t payload_len = ranges_size[0];

        if (payload_len >= 4 && memcmp(payload, "KOVS", 4) == 0) {
            if (!path_make_dirs(out_dir)) {
                arena_free(&a);
                return 0;
            }
            int ok = unpack_kvs_blob(job, payload, payload_len, out_dir, written);
            arena_free(&a);
            return ok;
        }

        if (!codec_looks_like_pairtable(payload, payload_len, &a)) {
            arena inner;
            arena_init(&inner);
            sub_layout nested_layout;
            if (nested_read_universal_layout(payload, payload_len, &inner, &nested_layout)) {
                size_t payload_end = estimate_layout_payload_end(payload, payload_len, &nested_layout, &inner);
                if (payload_end == 0 || payload_end > payload_len) {
                    payload_end = payload_len;
                }
                arena_free(&inner);
                if (!path_make_dirs(out_dir)) {
                    arena_free(&a);
                    return 0;
                }
                int ok = try_unpack_subcontainer(job, payload, payload_end, out_dir, depth + 1, written);
                arena_free(&a);
                return ok;
            }
            arena_free(&inner);
        }
    }

    if (!path_make_dirs(out_dir)) {
        arena_free(&a);
        return 0;
    }

    if (layout.kind == LAYOUT_MULTIBLOCK) {
        size_t out_index = 0;
        for (size_t i = 0; i < layout.primary.count; i++) {
            size_t off = layout.primary.offs[i];
            size_t sz = layout.primary.sizes[i];
            char name[64];
            if (sz == 0) {
                snprintf(name, sizeof(name), "%03zu.bin", out_index);
                write_child(job, out_dir, name, (const unsigned char *)"", 0, 0, depth, written);
                out_index++;
                continue;
            }
            const char *ext = nested_resolve_payload_ext(blob + off, sz);
            snprintf(name, sizeof(name), "%03zu%s", out_index, ext);
            write_child(job, out_dir, name, blob + off, sz,
                        nested_should_recurse(ext, blob + off, sz), depth, written);
            out_index++;
        }
        for (size_t b = 0; b < layout.later_count; b++) {
            for (size_t i = 0; i < layout.later[b].count; i++) {
                size_t off = layout.later[b].offs[i];
                size_t sz = layout.later[b].sizes[i];
                char name[64];
                if (sz == 0) {
                    snprintf(name, sizeof(name), "%03zu.bin", out_index);
                    write_child(job, out_dir, name, (const unsigned char *)"", 0, 0, depth, written);
                    out_index++;
                    continue;
                }
                const char *ext = nested_resolve_payload_ext(blob + off, sz);
                snprintf(name, sizeof(name), "%03zu%s", out_index, ext);
                write_child(job, out_dir, name, blob + off, sz,
                            nested_should_recurse(ext, blob + off, sz), depth, written);
                out_index++;
            }
        }
    } else if (layout.kind == LAYOUT_WRAPPER_PAIRS) {
        for (size_t i = 0; i < range_count; i++) {
            size_t off = ranges_off[i];
            size_t sz = ranges_size[i];
            const char *ext = nested_resolve_payload_ext(blob + off, sz);
            char name[64];
            snprintf(name, sizeof(name), "entry_%03zu%s", i, ext);
            write_child(job, out_dir, name, blob + off, sz,
                        nested_should_recurse(ext, blob + off, sz), depth, written);
        }
    } else if (layout.kind == LAYOUT_OFFSETS) {
        for (size_t i = 0; i < layout.unique_count; i++) {
            size_t start = layout.unique_offsets[i];
            size_t end = (i + 1 < layout.unique_count) ? layout.unique_offsets[i + 1] : n;
            if (end <= start) {
                continue;
            }
            size_t sz = end - start;
            const char *ext = nested_resolve_payload_ext(blob + start, sz);
            char name[64];
            snprintf(name, sizeof(name), "entry_%03zu%s", i, ext);
            write_child(job, out_dir, name, blob + start, sz,
                        nested_should_recurse(ext, blob + start, sz), depth, written);
        }
    } else if (layout.kind == LAYOUT_SEQUENTIAL) {
        size_t cur = layout.data_start;
        for (size_t i = 0; i < layout.seq_count; i++) {
            size_t sz = layout.seq_sizes[i];
            char name[64];
            if (sz == 0) {
                snprintf(name, sizeof(name), "%03zu.bin", i);
                write_child(job, out_dir, name, (const unsigned char *)"", 0, 0, depth, written);
                continue;
            }
            if (cur + sz > n) {
                break;
            }
            const char *ext = nested_resolve_payload_ext(blob + cur, sz);
            snprintf(name, sizeof(name), "%03zu%s", i, ext);
            write_child(job, out_dir, name, blob + cur, sz,
                        nested_should_recurse(ext, blob + cur, sz), depth, written);
            cur += sz;
        }
    } else {
        for (size_t i = 0; i < layout.count; i++) {
            size_t off = layout.offs[i];
            size_t sz = layout.sizes[i];
            if (sz == 0) {
                continue;
            }
            if (off + sz > n) {
                break;
            }
            const char *ext = nested_resolve_payload_ext(blob + off, sz);
            char name[64];
            snprintf(name, sizeof(name), "%03zu%s", i, ext);
            write_child(job, out_dir, name, blob + off, sz,
                        nested_should_recurse(ext, blob + off, sz), depth, written);
        }
    }

    arena_free(&a);
    return 1;
}

int nested_unpack_resource(job_ctx *job, const char *path, const unsigned char *blob, size_t len,
                           int depth, int64_t *written) {
    if (depth >= NESTED_MAX_DEPTH) {
        return 0;
    }
    if (job != NULL && job_cancelled(job)) {
        return 0;
    }

    char *out_dir = derive_out_dir(path);
    if (out_dir == NULL) {
        return 0;
    }

    int ok = 0;
    if (unpack_zp1_blob(job, blob, len, out_dir, depth, written)) {
        ok = 1;
    } else if (unpack_cstb_blob(job, blob, len, out_dir, depth, written)) {
        ok = 1;
    } else if (unpack_kvs_bank_blob(job, blob, len, out_dir, depth, written)) {
        ok = 1;
    } else if (unpack_colk_blob(job, blob, len, out_dir, depth, written)) {
        ok = 1;
    } else if (unpack_kvs_blob(job, blob, len, out_dir, written)) {
        ok = 1;
    } else if (unpack_mdlk_blob(job, blob, len, out_dir, written)) {
        ok = 1;
    } else if (unpack_kshl_blob(job, blob, len, out_dir, written)) {
        ok = 1;
    } else if (unpack_split_wrapper_blob(job, path, blob, len, depth, written)) {
        ok = 1;
    } else if (unpack_classic_split_resource(job, path, blob, len, depth, written)) {
        ok = 1;
    } else if (try_unpack_subcontainer(job, blob, len, out_dir, depth, written)) {
        ok = 1;
    } else if (unpack_embedded_mdlk_blob(job, blob, len, out_dir, depth, written)) {
        ok = 1;
    }

    free(out_dir);
    return ok;
}

int nested_read_embedded_mdlk_entries(const unsigned char *blob, size_t len, arena *a,
                                      size_t **offs_out, size_t **sizes_out, size_t *count_out) {
    if (len >= 16 && memcmp(blob, "MDLK", 4) == 0) {
        return 0;
    }

    size_t cap = 16;
    size_t *offsets = (size_t *)arena_alloc(a, sizeof(size_t) * cap);
    size_t *sizes = (size_t *)arena_alloc(a, sizeof(size_t) * cap);
    if (offsets == NULL || sizes == NULL) {
        return 0;
    }
    size_t found = 0;
    size_t search = 0;
    size_t previous_end = 0;

    for (;;) {
        size_t off = nested_find_magic(blob, len, search, "MDLK");
        if (off == (size_t)-1) {
            break;
        }
        scratch_node *node = scratch_get();
        mdlk_layout layout;
        int ok = node != NULL && nested_read_mdlk_layout(blob + off, len - off, &node->a, &layout);
        size_t payload_end = ok ? layout.payload_end : 0;
        scratch_put(node);

        if (!ok || payload_end <= 16 || off + payload_end > len || off < previous_end) {
            search = off + 1;
            continue;
        }
        if (found == cap) {
            size_t grown = cap * 2;
            size_t *no = (size_t *)arena_alloc(a, sizeof(size_t) * grown);
            size_t *ns = (size_t *)arena_alloc(a, sizeof(size_t) * grown);
            if (no == NULL || ns == NULL) {
                return 0;
            }
            memcpy(no, offsets, sizeof(size_t) * cap);
            memcpy(ns, sizes, sizeof(size_t) * cap);
            offsets = no;
            sizes = ns;
            cap = grown;
        }
        offsets[found] = off;
        sizes[found] = payload_end;
        found++;
        previous_end = off + payload_end;
        search = previous_end;
    }

    if (found == 0) {
        return 0;
    }
    *offs_out = offsets;
    *sizes_out = sizes;
    *count_out = found;
    return 1;
}

size_t nested_layout_expected_counts(const sub_layout *layout, size_t out[2]) {
    if (layout->kind == LAYOUT_MULTIBLOCK) {
        size_t total = layout->primary.count;
        for (size_t i = 0; i < layout->later_count; i++) {
            total += layout->later[i].count;
        }
        out[0] = total;
        return 1;
    }
    if (layout->kind == LAYOUT_WRAPPER_PAIRS) {
        out[0] = layout->wp_count;
        return 1;
    }
    if (layout->kind == LAYOUT_OFFSETS) {
        out[0] = layout->unique_count;
        return 1;
    }
    if (layout->kind == LAYOUT_SEQUENTIAL) {
        out[0] = layout->seq_count;
        return 1;
    }
    out[0] = layout->positive_count;
    out[1] = layout->count;
    return 2;
}

int nested_single_nested_payload(const unsigned char *blob, size_t len, const sub_layout *layout,
                                 arena *a, size_t *off_out, size_t *len_out, int *is_kvs) {
    size_t *offs = NULL;
    size_t *sizes = NULL;
    size_t count = nested_layout_payload_ranges(blob, len, layout, a, &offs, &sizes);
    if (count != 1) {
        return 0;
    }

    const unsigned char *payload = blob + offs[0];
    size_t payload_len = sizes[0];

    if (payload_len >= 4 && memcmp(payload, "KOVS", 4) == 0) {
        *off_out = offs[0];
        *len_out = payload_len;
        *is_kvs = 1;
        return 1;
    }
    if (codec_looks_like_pairtable(payload, payload_len, a)) {
        return 0;
    }

    sub_layout inner;
    if (!nested_read_universal_layout(payload, payload_len, a, &inner)) {
        return 0;
    }

    size_t *inner_offs = NULL;
    size_t *inner_sizes = NULL;
    size_t inner_count = nested_layout_payload_ranges(payload, payload_len, &inner, a,
                                                      &inner_offs, &inner_sizes);
    size_t payload_end = 0;
    for (size_t i = 0; i < inner_count; i++) {
        if (inner_offs[i] + inner_sizes[i] > payload_end) {
            payload_end = inner_offs[i] + inner_sizes[i];
        }
    }
    if (payload_end == 0 || payload_end > payload_len) {
        payload_end = payload_len;
    }

    *off_out = offs[0];
    *len_out = payload_end;
    *is_kvs = 0;
    return 1;
}
