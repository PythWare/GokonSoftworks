#ifndef CODEC_H
#define CODEC_H
#include "util.h"
#define SPLIT_MAX_CHUNKS 65535
#define PAIRTABLE_MAX_COUNT 4096

void codec_init(void);

void codec_set_big_endian(int big);
int codec_big_endian(void);

uint16_t codec_u16(const unsigned char *data, size_t off);
uint32_t codec_u32(const unsigned char *data, size_t off);
size_t codec_align_up(size_t value, size_t alignment);
int codec_looks_like_zlib_header(const unsigned char *data, size_t len, size_t off);

int codec_inflate(const unsigned char *data, size_t len, buf *out, err *e);
int codec_deflate(const unsigned char *data, size_t len, int level, buf *out, err *e);

int codec_zlib_header_anywhere(const unsigned char *data, size_t len, buf *out, err *e);

typedef struct {
    size_t offset;
    size_t payload_off;
    size_t payload_size;
    int compressed;
    uint32_t table_size;
} split_chunk;

typedef struct {
    uint16_t unk0;
    uint16_t file_type;
    uint16_t chunk_count;
    uint16_t unk1;
    uint32_t total_unc;
    size_t header_end;
    uint32_t *sizes;
    split_chunk *chunks;
} split_layout;

int codec_read_split_layout(const unsigned char *data, size_t len, arena *a, split_layout *out);
int codec_looks_like_classic_split(const unsigned char *data, size_t len, arena *a);
int codec_read_stored_layout(const unsigned char *data, size_t len, arena *a, split_layout *out);
int codec_looks_like_stored_split(const unsigned char *data, size_t len, arena *a);
int codec_looks_like_pairtable(const unsigned char *data, size_t len, arena *a);
int codec_looks_like_split(const unsigned char *data, size_t len, arena *a);
int codec_looks_like_empty_stub(const unsigned char *data, size_t len);
int codec_read_stored_split(const unsigned char *data, size_t len, uint64_t expected, buf *out);

int codec_decompress_classic_split(const unsigned char *data, size_t len, arena *a,
                                   buf *out, const char **ext_hint, err *e);
int codec_decompress_split(const unsigned char *data, size_t len, arena *a,
                           buf *out, const char **ext_hint, err *e);

const char *codec_detect_ext(const unsigned char *data, size_t len);
const char *codec_resolve_ext(const unsigned char *data, size_t len, const char *ext_hint);
const char *codec_match_ext_tables(const unsigned char *data, size_t len, size_t off);
const char *codec_dx9_ext_at(const unsigned char *data, size_t len, size_t off);

int codec_read_pairtable_entries(const unsigned char *data, size_t len, arena *a,
                                 size_t **offsets_out, size_t **sizes_out, uint32_t *count_out);

#endif
