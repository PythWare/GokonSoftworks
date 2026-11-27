#ifndef RECIPE_H
#define RECIPE_H
#include "json.h"
#include "util.h"
#define RECIPE_EXTENSION ".gokon"
#define RECIPE_MAGIC "GOKON\0"
#define RECIPE_MAGIC_LEN 6
#define RECIPE_FORMAT "gokon-mix"
#define RECIPE_VERSION 2
#define RECIPE_HEAD_LEN 9
#define RECIPE_MAX_PREVIEWS 5
#define RECIPE_DEFAULT_COLOUR "#7B1E3A"

typedef struct {
    int64_t idx_marker;
    int64_t entry_off;
    int64_t comp_marker;
    int64_t size;
    const char *digest;
    const char *container;
} recipe_entry;

typedef struct {
    const char *name;
    int64_t size;
    const char *sha256;
} recipe_asset;

typedef struct {
    arena a;
    char *path;
    const char *mod_id;
    int version;
    int game_index;
    json_value *header;
    int64_t data_start;
    recipe_entry *entries;
    size_t entry_count;
    const char *name;
    const char *game;
    const char *author;
    const char *version_text;
    const char *description;
    const char *colour;
    recipe_asset *images;
    size_t image_count;
    int has_audio;
    recipe_asset audio;
    int64_t payload_bytes;
    int64_t images_start;
    int64_t audio_start;
} gs_recipe;

typedef struct {
    gs_recipe *items;
    size_t count;
    char **problem_paths;
    char **problem_texts;
    size_t problem_count;
} recipe_scan;

typedef struct {
    char *key;
    char *path;
} recipe_match;

int recipe_read(const char *path, gs_recipe *out, err *e);
void recipe_free(gs_recipe *r);
int recipe_peek_game_index(const char *path);
int recipe_read_payload(const gs_recipe *r, size_t index, buf *out, err *e);
int recipe_read_images(const gs_recipe *r, buf *blobs, err *e);
int recipe_read_audio(const gs_recipe *r, buf *out, int *present, err *e);

int recipe_scan_folder(const char *folder, const char *game_id, int game_index, recipe_scan *out);
int recipe_list(const char *folder, const char *game_id, int game_index, recipe_scan *out);
void recipe_scan_free(recipe_scan *s);

int recipe_collect_sources(const char *source_folder, const char *const *records, size_t record_count,
                           recipe_match **out, size_t *count);
void recipe_matches_free(recipe_match *matches, size_t count);

#endif
