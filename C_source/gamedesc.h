#ifndef GAMEDESC_H
#define GAMEDESC_H
#include "util.h"
#define GAMEDESC_MAX_FIELDS 16
#define GAMEDESC_MAX_CANDIDATES 24
#define GAMEDESC_MAX_PARTS 32
#define GAMEDESC_MAX_FILES 48
#define GAMEDESC_NAME_MAX 260

typedef struct {
    char container[GAMEDESC_NAME_MAX];
    char toc[GAMEDESC_NAME_MAX];
    int64_t toc_offset;
    int64_t entry_size;
    int64_t alignment;
    int64_t count_offset;
    int64_t sector_field;
    int64_t offset_shift;
    int64_t field_size;
    int big_endian;
    const char *fields[GAMEDESC_MAX_FIELDS];
    int field_count;
    const char *shift_fields[GAMEDESC_MAX_FIELDS];
    int shift_field_count;
    const char *pack;
    int named;
    int patchable;
    int has_magic;
    int64_t magic;
    char candidates[GAMEDESC_MAX_CANDIDATES][GAMEDESC_NAME_MAX];
    int candidate_count;
} gs_part;

typedef struct {
    const char *game_id;
    const char *display_name;
    const char *unpack_folder;
    int game_index;
    int entry_size;
    int field_size;
    int has_cipher;
    int family;
    int has_parts;
    gs_part parts[GAMEDESC_MAX_PARTS];
    int part_count;
    const char *containers[GAMEDESC_MAX_FILES];
    int container_count;
    const char *idx_files[GAMEDESC_MAX_FILES];
    int idx_count;
} gs_game;

gs_game *gamedesc_load(const char *game_id);
gs_game *gamedesc_load_at(int index);
void gamedesc_free(gs_game *game);
const gs_part *gamedesc_part(const gs_game *game, int64_t idx_marker);

#endif
