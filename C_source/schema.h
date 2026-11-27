#ifndef SCHEMA_H
#define SCHEMA_H
#include "util.h"
#define SCHEMA_MAX_FIELDS 16
#define SCHEMA_MAX_FILES 24

typedef enum {
    SCHEMA_FAMILY_LINKDATA = 0,
    SCHEMA_FAMILY_LEGACY = 1,
    SCHEMA_FAMILY_LINKDATA_V2 = 2
} schema_family;

typedef struct {
    const char *game_id;
    const char *display_name;
    const char *containers[SCHEMA_MAX_FILES];
    int container_count;
    const char *idx_files[SCHEMA_MAX_FILES];
    int idx_count;
    const char *unpack_folder;
    int little_endian;
    int entry_size;
    int field_size;
    const char *fields[SCHEMA_MAX_FIELDS];
    int field_count;
    int64_t start_from_offset;
    int shift_bits;
    const char *shift_fields[SCHEMA_MAX_FIELDS];
    int shift_field_count;
    int has_cipher;
    int family;
} game_schema;

typedef struct {
    uint64_t offset;
    uint64_t orig_size;
    uint64_t comp_size;
    uint64_t comp_marker;
} idx_entry;

const game_schema *schema_find(const char *game_id);
const game_schema *schema_at(int index);
int schema_count(void);

int schema_field_index(const game_schema *s, const char *name);
uint64_t schema_read_field(const game_schema *s, const unsigned char *raw, int index);
void schema_write_field(const game_schema *s, unsigned char *raw, int index, uint64_t value);
int schema_read_entry(const game_schema *s, const unsigned char *raw, idx_entry *out);

#endif
