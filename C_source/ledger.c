#include "ledger.h"
#include "gsfs.h"
#include "gsjson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t hash_key(const char *text) {
    uint64_t h = 1469598103934665603ull;
    for (const unsigned char *c = (const unsigned char *)text; *c; c++) {
        h ^= *c;
        h *= 1099511628211ull;
    }
    return h;
}

static void map_init(tab_map *m) {
    memset(m, 0, sizeof(*m));
}

static void map_free(tab_map *m) {
    free(m->items);
    free(m->index);
    map_init(m);
}

static int map_reindex(tab_map *m) {
    size_t cap = 16;
    while (cap < m->count * 2 + 2) {
        cap <<= 1;
    }
    size_t *index = (size_t *)calloc(cap, sizeof(size_t));
    if (index == NULL) {
        return 0;
    }
    for (size_t i = 0; i < m->count; i++) {
        size_t at = (size_t)hash_key(m->items[i].key) & (cap - 1);
        while (index[at] != 0) {
            at = (at + 1) & (cap - 1);
        }
        index[at] = i + 1;
    }
    free(m->index);
    m->index = index;
    m->index_mask = cap - 1;
    return 1;
}

static long long map_find(const tab_map *m, const char *key) {
    if (m->index == NULL) {
        return -1;
    }
    size_t at = (size_t)hash_key(key) & m->index_mask;
    while (m->index[at] != 0) {
        size_t i = m->index[at] - 1;
        if (strcmp(m->items[i].key, key) == 0) {
            return (long long)i;
        }
        at = (at + 1) & m->index_mask;
    }
    return -1;
}

static int map_set(tab_map *m, arena *a, const char *key, json_value *value, int keep_old) {
    long long found = map_find(m, key);
    if (found >= 0) {
        if (!keep_old) {
            m->items[found].value = value;
        }
        return 1;
    }
    if (m->count == m->cap) {
        size_t grown = m->cap == 0 ? 16 : m->cap * 2;
        tab_item *bigger = (tab_item *)realloc(m->items, grown * sizeof(tab_item));
        if (bigger == NULL) {
            return 0;
        }
        m->items = bigger;
        m->cap = grown;
    }
    char *copy = arena_strdup(a, key, strlen(key));
    if (copy == NULL) {
        return 0;
    }
    m->items[m->count].key = copy;
    m->items[m->count].value = value;
    m->count++;
    if (m->index == NULL || m->count * 2 + 2 > m->index_mask + 1) {
        return map_reindex(m);
    }
    size_t at = (size_t)hash_key(copy) & m->index_mask;
    while (m->index[at] != 0) {
        at = (at + 1) & m->index_mask;
    }
    m->index[at] = m->count;
    return 1;
}

static json_value *map_remove(tab_map *m, const char *key) {
    long long found = map_find(m, key);
    if (found < 0) {
        return NULL;
    }
    json_value *value = m->items[found].value;
    memmove(&m->items[found], &m->items[found + 1], (m->count - (size_t)found - 1) * sizeof(tab_item));
    m->count--;
    map_reindex(m);
    return value;
}

static void map_clear(tab_map *m) {
    m->count = 0;
    if (m->index != NULL) {
        memset(m->index, 0, (m->index_mask + 1) * sizeof(size_t));
    }
}

char *ledger_path(const char *taildata_dir, const char *game_id) {
    char name[300];
    snprintf(name, sizeof(name), "%s%s", game_id, LEDGER_SUFFIX);
    char *dir = taildata_dir != NULL ? _strdup(taildata_dir) : gs_taildata_dir();
    if (dir == NULL) {
        return NULL;
    }
    path_make_dirs(dir);
    char *path = path_join(dir, name);
    free(dir);
    return path;
}

void ledger_slot_key(int64_t idx_marker, int64_t entry_off, char out[48]) {
    snprintf(out, 48, "%lld:%lld", (long long)idx_marker, (long long)entry_off);
}

int tab_init(gs_tab *tab, const char *taildata_dir, const char *game_dir, const char *game_id) {
    memset(tab, 0, sizeof(*tab));
    arena_init(&tab->a);
    map_init(&tab->vanilla);
    map_init(&tab->mods);
    char *normal = gs_path_normalize(game_dir == NULL ? "" : game_dir);
    char *path = ledger_path(taildata_dir, game_id);
    if (normal == NULL || path == NULL) {
        free(normal);
        free(path);
        return 0;
    }
    tab->game_dir = arena_strdup(&tab->a, normal, strlen(normal));
    tab->path = arena_strdup(&tab->a, path, strlen(path));
    tab->game_id = arena_strdup(&tab->a, game_id, strlen(game_id));
    free(normal);
    free(path);
    return tab->game_dir != NULL && tab->path != NULL && tab->game_id != NULL;
}

static int set_size(gs_tab *tab, const char *key, int64_t size, int keep_old) {
    for (size_t i = 0; i < tab->size_count; i++) {
        if (strcmp(tab->sizes[i].key, key) == 0) {
            if (!keep_old) {
                tab->sizes[i].size = size;
            }
            return 1;
        }
    }
    if (tab->size_count == tab->size_cap) {
        size_t grown = tab->size_cap == 0 ? 16 : tab->size_cap * 2;
        tab_size *bigger = (tab_size *)realloc(tab->sizes, grown * sizeof(tab_size));
        if (bigger == NULL) {
            return 0;
        }
        tab->sizes = bigger;
        tab->size_cap = grown;
    }
    char *copy = arena_strdup(&tab->a, key, strlen(key));
    if (copy == NULL) {
        return 0;
    }
    tab->sizes[tab->size_count].key = copy;
    tab->sizes[tab->size_count].size = size;
    tab->size_count++;
    return 1;
}

int tab_load(gs_tab *tab) {
    json_value *data = gsjson_read_file(&tab->a, tab->path);
    if (data == NULL || data->kind != JSON_OBJ) {
        return 1;
    }
    const json_value *version = json_obj_get(data, "version");
    if (version == NULL || version->kind != JSON_NUM || version->number != (double)LEDGER_VERSION) {
        return 1;
    }
    const char *game = json_as_str(json_obj_get(data, "game"), NULL);
    if (game == NULL || strcmp(game, tab->game_id) != 0) {
        return 1;
    }
    const json_value *recorded = json_obj_get(data, "game_dir");
    if (recorded != NULL && recorded->kind == JSON_STR && recorded->text_len > 0 &&
        !gs_path_equal(recorded->text, tab->game_dir)) {
        gs_log("WARNING", "Ignoring the %s tab: it was taken against %s, not %s", tab->game_id,
               recorded->text, tab->game_dir);
        return 1;
    }

    const json_value *sizes = json_obj_get(data, "container_sizes");
    if (sizes != NULL && sizes->kind == JSON_OBJ) {
        for (size_t i = 0; i < sizes->count; i++) {
            set_size(tab, sizes->keys[i], json_as_i64(sizes->items[i], 0), 0);
        }
    }
    const json_value *vanilla = json_obj_get(data, "vanilla_slots");
    if (vanilla != NULL && vanilla->kind == JSON_OBJ) {
        for (size_t i = 0; i < vanilla->count; i++) {
            map_set(&tab->vanilla, &tab->a, vanilla->keys[i], vanilla->items[i], 0);
        }
    }
    const json_value *mods = json_obj_get(data, "mods");
    if (mods != NULL && mods->kind == JSON_OBJ) {
        for (size_t i = 0; i < mods->count; i++) {
            map_set(&tab->mods, &tab->a, mods->keys[i], mods->items[i], 0);
        }
    }
    return 1;
}

int tab_open(gs_tab *tab, const char *taildata_dir, const char *game_dir, const char *game_id) {
    if (!tab_init(tab, taildata_dir, game_dir, game_id)) {
        return 0;
    }
    return tab_load(tab);
}

static json_value *map_to_json(const tab_map *m, arena *a) {
    char **keys = (char **)malloc((m->count + 1) * sizeof(char *));
    json_value **values = (json_value **)malloc((m->count + 1) * sizeof(json_value *));
    json_value *node = NULL;
    if (keys != NULL && values != NULL) {
        for (size_t i = 0; i < m->count; i++) {
            keys[i] = m->items[i].key;
            values[i] = m->items[i].value;
        }
        node = jv_obj_from(a, keys, values, m->count);
    }
    free(keys);
    free(values);
    return node;
}

json_value *tab_to_json(gs_tab *tab, arena *a) {
    size_t n = tab->size_count;
    char **size_keys = (char **)malloc((n + 1) * sizeof(char *));
    json_value **size_values = (json_value **)malloc((n + 1) * sizeof(json_value *));
    json_value *sizes = NULL;
    if (size_keys != NULL && size_values != NULL) {
        int ok = 1;
        for (size_t i = 0; i < n && ok; i++) {
            size_keys[i] = tab->sizes[i].key;
            size_values[i] = jv_num(a, (double)tab->sizes[i].size);
            ok = size_values[i] != NULL;
        }
        if (ok) {
            sizes = jv_obj_from(a, size_keys, size_values, n);
        }
    }
    free(size_keys);
    free(size_values);
    json_value *vanilla = map_to_json(&tab->vanilla, a);
    json_value *mods = map_to_json(&tab->mods, a);
    if (sizes == NULL || vanilla == NULL || mods == NULL) {
        return NULL;
    }
    char *keys[6] = {"version", "game", "game_dir", "container_sizes", "vanilla_slots", "mods"};
    json_value *values[6] = {jv_num(a, LEDGER_VERSION), jv_str(a, tab->game_id),
                             jv_str(a, tab->game_dir), sizes, vanilla, mods};
    for (int i = 0; i < 6; i++) {
        if (values[i] == NULL) {
            return NULL;
        }
    }
    return jv_obj_from(a, keys, values, 6);
}

int tab_save(gs_tab *tab, err *e) {
    arena a;
    arena_init(&a);
    json_value *payload = tab_to_json(tab, &a);
    int ok = payload != NULL && gsjson_write_file(tab->path, payload);
    arena_free(&a);
    if (!ok) {
        err_set(e, "Couldnt update the tab: couldnt write %s", tab->path);
    }
    return ok;
}

void tab_free(gs_tab *tab) {
    free(tab->sizes);
    map_free(&tab->vanilla);
    map_free(&tab->mods);
    arena_free(&tab->a);
    memset(tab, 0, sizeof(*tab));
}

int tab_measured(const gs_tab *tab) {
    return tab->size_count > 0;
}

int tab_remember_size(gs_tab *tab, int64_t idx_marker, int64_t size) {
    char key[32];
    snprintf(key, sizeof(key), "%lld", (long long)idx_marker);
    return set_size(tab, key, size, 1);
}

int tab_original_size(const gs_tab *tab, int64_t idx_marker, int64_t *size) {
    char key[32];
    snprintf(key, sizeof(key), "%lld", (long long)idx_marker);
    for (size_t i = 0; i < tab->size_count; i++) {
        if (strcmp(tab->sizes[i].key, key) == 0) {
            *size = tab->sizes[i].size;
            return 1;
        }
    }
    return 0;
}

int tab_remember_slot(gs_tab *tab, int64_t idx_marker, int64_t entry_off, const char *original) {
    char key[48];
    ledger_slot_key(idx_marker, entry_off, key);
    if (map_find(&tab->vanilla, key) >= 0) {
        return 1;
    }
    json_value *value = jv_str(&tab->a, original);
    return value != NULL && map_set(&tab->vanilla, &tab->a, key, value, 1);
}

const json_value *tab_vanilla_for(const gs_tab *tab, int64_t idx_marker, int64_t entry_off) {
    char key[48];
    ledger_slot_key(idx_marker, entry_off, key);
    long long found = map_find(&tab->vanilla, key);
    return found < 0 ? NULL : tab->vanilla.items[found].value;
}

tab_slot *tab_all_vanilla_slots(const gs_tab *tab, size_t *count) {
    tab_slot *out = (tab_slot *)calloc(tab->vanilla.count + 1, sizeof(tab_slot));
    *count = 0;
    if (out == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < tab->vanilla.count; i++) {
        const char *key = tab->vanilla.items[i].key;
        const char *colon = strchr(key, ':');
        out[i].idx_marker = strtoll(key, NULL, 10);
        out[i].entry_off = colon == NULL ? 0 : strtoll(colon + 1, NULL, 10);
        const json_value *value = tab->vanilla.items[i].value;
        out[i].original = value != NULL && value->kind == JSON_STR ? value->text : NULL;
    }
    *count = tab->vanilla.count;
    return out;
}

int tab_is_enabled(const gs_tab *tab, const char *mod_id) {
    return map_find(&tab->mods, mod_id) >= 0;
}

static int compare_text(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

char **tab_enabled_ids(const gs_tab *tab, size_t *count) {
    char **ids = (char **)calloc(tab->mods.count + 1, sizeof(char *));
    *count = 0;
    if (ids == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < tab->mods.count; i++) {
        ids[i] = tab->mods.items[i].key;
    }
    qsort(ids, tab->mods.count, sizeof(char *), compare_text);
    *count = tab->mods.count;
    return ids;
}

int tab_record(gs_tab *tab, const char *mod_id, json_value *slots) {
    return map_set(&tab->mods, &tab->a, mod_id, slots, 0);
}

json_value *tab_forget(gs_tab *tab, const char *mod_id) {
    return map_remove(&tab->mods, mod_id);
}

void tab_clear(gs_tab *tab) {
    map_clear(&tab->mods);
}

void tab_clear_vanilla(gs_tab *tab) {
    map_clear(&tab->vanilla);
}
