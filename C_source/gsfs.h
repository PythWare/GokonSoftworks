#ifndef GSFS_H
#define GSFS_H
#include "util.h"

typedef void (*gs_log_sink)(void *ctx, const char *level, const char *text);

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} gs_names;

void gs_set_package_root(const char *path);
const char *gs_package_root(void);
const char *gs_project_root(void);
char *gs_png_dir(void);
char *gs_taildata_dir(void);
char *gs_filenames_dir(void);
char *gs_mods_dir(void);
char *gs_log_path(void);
char *gs_taildata_path(const char *game_id);

void gs_log_set_sink(gs_log_sink sink, void *ctx);
void gs_log(const char *level, const char *fmt, ...);

void gs_human_size(int64_t size, char *out, size_t room);
void gs_format_bytes(int64_t size, int known, char *out, size_t room);
int64_t gs_align_up(int64_t value, int64_t alignment);

char *gs_path_normalize(const char *path);
int gs_path_equal(const char *a, const char *b);
const char *gs_path_name(const char *path);
char *gs_path_stem(const char *path);
char *gs_path_full(const char *path);
char *gs_path_parent(const char *path);
int gs_casefold_equal(const char *a, const char *b);
int gs_casefold_compare(const char *a, const char *b);

void gs_names_init(gs_names *list);
int gs_names_push(gs_names *list, const char *text);
void gs_names_free(gs_names *list);
int gs_list_dir(const char *folder, gs_names *out);
int gs_list_tree_files(const char *folder, gs_names *out);
void gs_names_sort_paths(gs_names *list);

int gs_wants_region_pair(const char *name);
int gs_region_pair(const char *folder, char **bin_out, char **toc_out);

#endif
