#define WIN32_LEAN_AND_MEAN
#include "settings.h"
#include "gsfs.h"
#include "gsjson.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

static const gs_theme themes[] = {
    {"sakura_fizz", "Sakura Fizz", 0xE26F9E, 0xFFF86B, 0x140d15, 0x221420, 0x33202f, 0x0e090f,
     0xfff4fa, 0xdcc0cf, 0xFFF86B, 0x8ff0b4, 0xff8098, 0x3a2029, 0xE26F9E},
    {"midori_sour", "Midori Sour", 0x58B082, 0xEB5851, 0x0d1410, 0x152219, 0x213326, 0x080f0b,
     0xf2fff6, 0xbfd8c8, 0xEB5851, 0x9ef0bd, 0xff9a86, 0x20301f, 0x58B082},
    {"ramune_blue", "Ramune Blue", 0x4FC3C7, 0xC467BF, 0x0b1116, 0x141d24, 0x1f2c35, 0x070c10,
     0xf0fbff, 0xb8ccd8, 0xC467BF, 0x8ff0d5, 0xff90b8, 0x1b2a30, 0x4FC3C7},
    {"ume_sour", "Ume Sour", 0xEADCB9, 0xA01C1C, 0x15100c, 0x211a14, 0x33281e, 0x0f0b08,
     0xfff8ee, 0xd6c4ac, 0xEADCB9, 0x9fe6a8, 0xff8f7a, 0x2e211a, 0xA01C1C},
};

static uint64_t random_state;

static uint64_t next_random(void) {
    if (random_state == 0) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        random_state = (uint64_t)now.QuadPart ^ ((uint64_t)GetCurrentProcessId() << 32) ^ 0x9E3779B97F4A7C15ull;
    }
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return random_state;
}

int theme_count(void) {
    return (int)(sizeof(themes) / sizeof(themes[0]));
}

const gs_theme *theme_at(int index) {
    return index >= 0 && index < theme_count() ? &themes[index] : NULL;
}

const gs_theme *theme_find(const char *key) {
    for (int i = 0; key != NULL && i < theme_count(); i++) {
        if (strcmp(themes[i].key, key) == 0) {
            return &themes[i];
        }
    }
    return NULL;
}

const gs_theme *theme_pick(const char *previous_key) {
    const gs_theme *others[8];
    int count = 0;
    for (int i = 0; i < theme_count(); i++) {
        if (previous_key == NULL || strcmp(themes[i].key, previous_key) != 0) {
            others[count++] = &themes[i];
        }
    }
    if (count == 0) {
        return &themes[next_random() % (uint64_t)theme_count()];
    }
    const gs_theme *previous = theme_find(previous_key);
    if (previous != NULL && (double)(next_random() % 1000000) / 1000000.0 < 0.15) {
        return previous;
    }
    return others[next_random() % (uint64_t)count];
}

int colour_parse(const char *text, uint32_t *out) {
    if (text == NULL) {
        return 0;
    }
    if (*text == '#') {
        text++;
    }
    size_t len = strlen(text);
    char expanded[7];
    if (len == 3) {
        for (int i = 0; i < 3; i++) {
            expanded[i * 2] = text[i];
            expanded[i * 2 + 1] = text[i];
        }
        expanded[6] = 0;
        text = expanded;
        len = 6;
    }
    if (len != 6) {
        return 0;
    }
    uint32_t value = 0;
    for (size_t i = 0; i < 6; i++) {
        char ch = text[i];
        int digit = ch >= '0' && ch <= '9' ? ch - '0'
                    : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                    : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
        if (digit < 0) {
            return 0;
        }
        value = (value << 4) | (uint32_t)digit;
    }
    *out = value;
    return 1;
}

static void obj_set(arena *a, json_value *obj, const char *key, json_value *value) {
    if (obj == NULL || obj->kind != JSON_OBJ || value == NULL) {
        return;
    }
    for (size_t i = 0; i < obj->count; i++) {
        if (strcmp(obj->keys[i], key) == 0) {
            obj->items[i] = value;
            return;
        }
    }
    char **keys = (char **)arena_alloc(a, (obj->count + 1) * sizeof(char *));
    json_value **items = (json_value **)arena_alloc(a, (obj->count + 1) * sizeof(json_value *));
    char *copy = arena_strdup(a, key, strlen(key));
    if (keys == NULL || items == NULL || copy == NULL) {
        return;
    }
    if (obj->count > 0) {
        memcpy(keys, obj->keys, obj->count * sizeof(char *));
        memcpy(items, obj->items, obj->count * sizeof(json_value *));
    }
    keys[obj->count] = copy;
    items[obj->count] = value;
    obj->keys = keys;
    obj->items = items;
    obj->count++;
}

int settings_load(gs_settings *s, const char *project_root) {
    memset(s, 0, sizeof(*s));
    arena_init(&s->a);
    char *path = path_join(project_root, SETTINGS_FILENAME);
    if (path == NULL) {
        return 0;
    }
    s->path = arena_strdup(&s->a, path, strlen(path));
    free(path);
    s->root = jv_obj_from(&s->a, NULL, NULL, 0);
    if (s->root == NULL) {
        return 0;
    }
    obj_set(&s->a, s->root, "last_theme", jv_str(&s->a, ""));
    obj_set(&s->a, s->root, "output_dir", jv_str(&s->a, ""));
    obj_set(&s->a, s->root, "volume_path", jv_str(&s->a, ""));
    obj_set(&s->a, s->root, "mod_audio", jv_bool(&s->a, 1));
    if (!path_is_file(s->path)) {
        return 1;
    }
    json_value *file = gsjson_read_file(&s->a, s->path);
    if (file != NULL && file->kind == JSON_OBJ) {
        for (size_t i = 0; i < file->count; i++) {
            obj_set(&s->a, s->root, file->keys[i], file->items[i]);
        }
    } else if (file != NULL) {
        gs_log("WARNING", "Ignoring unreadable settings file: it isnt a JSON object");
    }
    return 1;
}

void settings_save(gs_settings *s) {
    if (s->root == NULL || !gsjson_write_file(s->path, s->root)) {
        gs_log("WARNING", "Couldnt save settings: couldnt write %s", s->path == NULL ? "" : s->path);
    }
}

void settings_free(gs_settings *s) {
    arena_free(&s->a);
    memset(s, 0, sizeof(*s));
}

const char *settings_get_str(gs_settings *s, const char *key, const char *fallback) {
    return json_as_str(json_obj_get(s->root, key), fallback);
}

int settings_get_bool(gs_settings *s, const char *key, int fallback) {
    const json_value *node = json_obj_get(s->root, key);
    if (node == NULL) {
        return fallback;
    }
    if (node->kind == JSON_STR) {
        return node->text_len > 0;
    }
    if (node->kind == JSON_NULL) {
        return 0;
    }
    if (node->kind == JSON_ARR || node->kind == JSON_OBJ) {
        return node->count > 0;
    }
    return json_as_bool(node, fallback);
}

void settings_set_str(gs_settings *s, const char *key, const char *value) {
    obj_set(&s->a, s->root, key, jv_str(&s->a, value == NULL ? "" : value));
}

void settings_set_bool(gs_settings *s, const char *key, int value) {
    obj_set(&s->a, s->root, key, jv_bool(&s->a, value));
}

static json_value *game_dirs(gs_settings *s) {
    json_value *dirs = json_obj_get(s->root, "game_dirs");
    if (dirs == NULL) {
        dirs = jv_obj_from(&s->a, NULL, NULL, 0);
        obj_set(&s->a, s->root, "game_dirs", dirs);
    }
    return dirs;
}

const char *settings_game_dir(gs_settings *s, const char *game_id) {
    json_value *dirs = game_dirs(s);
    const char *dir = json_as_str(json_obj_get(dirs, game_id), NULL);
    return dir != NULL && dir[0] != 0 ? dir : NULL;
}

void settings_set_game_dir(gs_settings *s, const char *game_id, const char *dir) {
    json_value *dirs = game_dirs(s);
    if (dirs != NULL && dirs->kind == JSON_OBJ) {
        obj_set(&s->a, dirs, game_id, jv_str(&s->a, dir));
    }
}
