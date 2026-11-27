#ifndef LEDGER_H
#define LEDGER_H
#include "json.h"
#include "util.h"
#define LEDGER_SUFFIX ".tab.json"
#define LEDGER_VERSION 2

typedef struct {
    char *key;
    int64_t size;
} tab_size;

typedef struct {
    char *key;
    json_value *value;
} tab_item;

typedef struct {
    tab_item *items;
    size_t count;
    size_t cap;
    size_t *index;
    size_t index_mask;
} tab_map;

typedef struct {
    int64_t idx_marker;
    int64_t entry_off;
    const char *original;
} tab_slot;

typedef struct {
    arena a;
    char *game_dir;
    char *game_id;
    char *path;
    tab_size *sizes;
    size_t size_count;
    size_t size_cap;
    tab_map vanilla;
    tab_map mods;
} gs_tab;

char *ledger_path(const char *taildata_dir, const char *game_id);
void ledger_slot_key(int64_t idx_marker, int64_t entry_off, char out[48]);

int tab_init(gs_tab *tab, const char *taildata_dir, const char *game_dir, const char *game_id);
int tab_load(gs_tab *tab);
int tab_open(gs_tab *tab, const char *taildata_dir, const char *game_dir, const char *game_id);
int tab_save(gs_tab *tab, err *e);
void tab_free(gs_tab *tab);

int tab_measured(const gs_tab *tab);
int tab_remember_size(gs_tab *tab, int64_t idx_marker, int64_t size);
int tab_original_size(const gs_tab *tab, int64_t idx_marker, int64_t *size);
int tab_remember_slot(gs_tab *tab, int64_t idx_marker, int64_t entry_off, const char *original);
const json_value *tab_vanilla_for(const gs_tab *tab, int64_t idx_marker, int64_t entry_off);
tab_slot *tab_all_vanilla_slots(const gs_tab *tab, size_t *count);
int tab_is_enabled(const gs_tab *tab, const char *mod_id);
char **tab_enabled_ids(const gs_tab *tab, size_t *count);
int tab_record(gs_tab *tab, const char *mod_id, json_value *slots);
json_value *tab_forget(gs_tab *tab, const char *mod_id);
void tab_clear(gs_tab *tab);
void tab_clear_vanilla(gs_tab *tab);
json_value *tab_to_json(gs_tab *tab, arena *a);

#endif
