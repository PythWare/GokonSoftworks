#ifndef SETTINGS_H
#define SETTINGS_H
#include "json.h"
#include "util.h"
#define SETTINGS_FILENAME "gokonsoftworks_settings.json"

typedef struct {
    const char *key;
    const char *name;
    uint32_t liquid_bottom;
    uint32_t liquid_top;
    uint32_t bg;
    uint32_t panel;
    uint32_t panel_soft;
    uint32_t field;
    uint32_t text;
    uint32_t text_muted;
    uint32_t accent;
    uint32_t ok;
    uint32_t danger;
    uint32_t shelf;
    uint32_t shelf_edge;
} gs_theme;

typedef struct {
    arena a;
    char *path;
    json_value *root;
} gs_settings;

int theme_count(void);
const gs_theme *theme_at(int index);
const gs_theme *theme_find(const char *key);
const gs_theme *theme_pick(const char *previous_key);
int colour_parse(const char *text, uint32_t *out);

int settings_load(gs_settings *s, const char *project_root);
void settings_save(gs_settings *s);
void settings_free(gs_settings *s);
const char *settings_get_str(gs_settings *s, const char *key, const char *fallback);
int settings_get_bool(gs_settings *s, const char *key, int fallback);
void settings_set_str(gs_settings *s, const char *key, const char *value);
void settings_set_bool(gs_settings *s, const char *key, int value);
const char *settings_game_dir(gs_settings *s, const char *game_id);
void settings_set_game_dir(gs_settings *s, const char *game_id, const char *dir);

#endif
