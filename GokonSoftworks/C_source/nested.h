#ifndef NESTED_H
#define NESTED_H
#include "proto.h"
#include "util.h"
#define NESTED_MAX_COUNT 100000
#define NESTED_MAX_DEPTH 64
#define NESTED_KVS_BANK_HEADER 0x800

typedef enum {
    LAYOUT_NONE = 0,
    LAYOUT_WRAPPER_PAIRS,
    LAYOUT_MULTIBLOCK,
    LAYOUT_PAIRTABLE,
    LAYOUT_OFFSETS,
    LAYOUT_SEQUENTIAL
} layout_kind;

typedef enum {
    BLOCK_NONE = 0,
    BLOCK_RELPAIR,
    BLOCK_RELPAIRTABLE,
    BLOCK_SIMPLE,
    BLOCK_RAW
} block_kind;

typedef struct {
    block_kind kind;
    int big_endian;
    size_t start;
    size_t end;
    size_t count;
    size_t *offs;
    size_t *sizes;
    size_t *rels;
    size_t declared_count;
    size_t entry_count;
    size_t payload_base_rel;
    size_t payload_base_abs;
    size_t table_end;
    size_t reserved_start;
} sub_block;

typedef struct {
    layout_kind kind;
    int big_endian;

    size_t count;
    size_t *offs;
    size_t *sizes;
    size_t positive_count;
    size_t *positive;
    size_t table_end;

    size_t unique_count;
    size_t *unique_offsets;

    size_t data_start;
    size_t seq_count;
    size_t *seq_sizes;
    size_t child_align;

    size_t outer_count;
    size_t primary_block_off;
    sub_block primary;
    size_t later_count;
    sub_block *later;
    size_t *later_block_offsets;
    size_t later_offset_count;
    size_t last_block_span;
    int has_last_block_span;
    size_t tail_field_off;
    int has_tail_field;

    size_t pair_count;
    size_t *pair_cursor;
    size_t *wp_off;
    size_t *wp_size;
    size_t wp_count;
    size_t wrapper_end;
} sub_layout;

typedef struct {
    size_t offset;
    size_t size;
    const char *ext;
} res_entry;

typedef struct {
    size_t count;
    res_entry *entries;
    size_t payload_end;
} mdlk_layout;

typedef struct {
    size_t size;
    size_t payload_start;
    size_t payload_size;
    size_t payload_end;
    size_t count;
    res_entry *entries;
} kshl_layout;

size_t nested_find_magic(const unsigned char *blob, size_t len, size_t from, const char *magic);
int nested_read_mdlk_layout(const unsigned char *blob, size_t len, arena *a, mdlk_layout *out);
int nested_read_kshl_layout(const unsigned char *blob, size_t len, arena *a, kshl_layout *out);
int nested_looks_like_kshl(const unsigned char *blob, size_t len);
int nested_looks_like_colk(const unsigned char *blob, size_t len);
int nested_colk_endian(const unsigned char *blob, size_t len);
size_t nested_colk_children(const unsigned char *blob, size_t len, size_t index,
                            size_t *start_out);
int nested_looks_like_embedded_mdlk(const unsigned char *blob, size_t len);
size_t nested_kvs_bank_count(const unsigned char *blob, size_t len);
int nested_read_embedded_mdlk_entries(const unsigned char *blob, size_t len, arena *a,
                                      size_t **offs_out, size_t **sizes_out, size_t *count_out);
size_t nested_layout_expected_counts(const sub_layout *layout, size_t out[2]);
int nested_single_nested_payload(const unsigned char *blob, size_t len, const sub_layout *layout,
                                 arena *a, size_t *off_out, size_t *len_out, int *is_kvs);

int nested_read_universal_layout(const unsigned char *blob, size_t len, arena *a, sub_layout *out);
int nested_payload_looks_meaningful(const unsigned char *blob, size_t len, int allow_split_wrapper);
const char *nested_match_known_signature(const unsigned char *blob, size_t len, size_t off);
int nested_looks_like_structure(const unsigned char *blob, size_t len);
size_t nested_layout_payload_ranges(const unsigned char *blob, size_t len, const sub_layout *layout,
                                    arena *a, size_t **offs_out, size_t **sizes_out);
const char *nested_resolve_payload_ext(const unsigned char *blob, size_t len);
int nested_should_recurse(const char *ext, const unsigned char *blob, size_t len);

int nested_unpack_resource(job_ctx *job, const char *path, const unsigned char *blob, size_t len,
                           int depth, int64_t *written);

void nested_thread_cleanup(void);

#endif
