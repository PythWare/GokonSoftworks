#ifndef LEGACY_H
#define LEGACY_H
#include "proto.h"
#include "unpack.h"
#include "util.h"
#define LEGACY_SECTOR 2048
#define LEGACY_PART_MAX 8
#define LEGACY_NAME_MAX 260
#define LEGACY_MAX_FIELDS 8

typedef enum {
    LEGACY_TABLE_MDATA = 0,
    LEGACY_TABLE_SELF,
    LEGACY_TABLE_NONE,
    LEGACY_TABLE_BANKS,
    LEGACY_TABLE_ACCUM
} legacy_table_kind;

typedef enum {
    LEGACY_SCAN_NONE = 0,
    LEGACY_SCAN_NEXT_MAGIC,
    LEGACY_SCAN_SIZED
} legacy_scan_kind;

typedef struct {
    legacy_table_kind table;
    const char *container;
    const char *toc;
    int64_t toc_offset;
    int64_t count_offset;
    int64_t sector_field;
    int entry_size;

    int field_size;
    int big_endian;
    const char *fields[LEGACY_MAX_FIELDS];
    int field_count;
    int shift_bits;
    const char *shift_fields[LEGACY_MAX_FIELDS];
    int shift_field_count;

    const char *pack;
    int named;
    int appendable;

    legacy_scan_kind scan;
    const char *scan_magic;
    int scan_magic_len;
    int64_t scan_start;
    int64_t scan_align;
    int64_t sized_header;
    int64_t sized_len_off;

    int deep_nested;
    int64_t row_count;
    int chunk_table;
} legacy_part;

int legacy_field_index(const legacy_part *p, const char *name);
uint64_t legacy_read_field(const legacy_part *p, const unsigned char *raw, int index);
uint64_t legacy_read_named(const legacy_part *p, const unsigned char *raw, const char *name);

typedef struct {
    legacy_part items[LEGACY_PART_MAX];
    char bins[LEGACY_PART_MAX][LEGACY_NAME_MAX];
    char tocs[LEGACY_PART_MAX][LEGACY_NAME_MAX];
    int count;
} legacy_part_set;

int legacy_region_pair(const char *base_dir, const char *container, const char *toc,
                       char *bin_out, char *toc_out, size_t room);
const legacy_part *legacy_parts_for(const char *game_id, int *count);
const legacy_part *legacy_parts_in(job_ctx *job, const char *base_dir, const char *game_id,
                                   int *count, legacy_part_set *set);
const legacy_part *legacy_part_at(const char *game_id, int index);
int legacy_part_count(const char *game_id);

int legacy_pd2_parse(const unsigned char *blob, size_t len, arena *a,
                     size_t **offs_out, size_t **sizes_out, size_t *count_out);
int legacy_looks_like_pd2(const unsigned char *blob, size_t len);
const char *legacy_pd2_child_ext(const unsigned char *data, size_t len);

int legacy_run(job_ctx *job, const unpack_opts *opts, unpack_stats *stats,
               manifest_writer *manifest, err *e);

typedef struct {
    char container[64];
    int64_t entries;
    int64_t bytes;
    char out_path[512];
} legacy_rebuilt;

typedef struct {
    legacy_rebuilt items[LEGACY_PART_MAX];
    int count;
} legacy_rebuild_stats;

int legacy_rebuild(job_ctx *job, const char *game_id, const char *base_dir,
                   const char *src_root, const char *unpack_folder,
                   const char *out_dir, const char *ref_dir,
                   legacy_rebuild_stats *stats, err *e);

#endif
