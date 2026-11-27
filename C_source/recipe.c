#include "recipe.h"
#include "gsfs.h"
#include <stdlib.h>
#include <string.h>

static uint32_t read_u32le(const unsigned char *raw) {
    return (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16) |
           ((uint32_t)raw[3] << 24);
}

static const char *header_text(const json_value *header, const char *key, const char *fallback) {
    const json_value *node = json_obj_get(header, key);
    if (node == NULL) {
        return fallback;
    }
    return node->kind == JSON_STR ? node->text : "";
}

static int read_asset(arena *a, const json_value *node, recipe_asset *out) {
    out->name = arena_strdup(a, "", 0);
    out->sha256 = out->name;
    out->size = 0;
    if (node == NULL || node->kind != JSON_OBJ) {
        return 0;
    }
    out->name = header_text(node, "name", "");
    out->sha256 = header_text(node, "sha256", "");
    out->size = json_as_i64(json_obj_get(node, "size"), 0);
    return 1;
}

static int json_truthy(const json_value *node) {
    if (node == NULL) {
        return 0;
    }
    switch (node->kind) {
        case JSON_NULL: return 0;
        case JSON_BOOL: return node->boolean;
        case JSON_NUM: return node->number != 0.0;
        case JSON_STR: return node->text_len > 0;
        default: return node->count > 0;
    }
}

int recipe_read(const char *path, gs_recipe *out, err *e) {
    memset(out, 0, sizeof(*out));
    arena_init(&out->a);
    out->path = arena_strdup(&out->a, path, strlen(path));
    if (out->path == NULL) {
        err_set(e, "out of memory");
        return 0;
    }
    const char *name = gs_path_name(out->path);
    out->mod_id = name;

    FILE *handle = file_open(path, "rb");
    if (handle == NULL) {
        err_set(e, "Couldnt read %s: it couldnt be opened", name);
        return 0;
    }
    unsigned char magic[RECIPE_MAGIC_LEN];
    unsigned char head[RECIPE_HEAD_LEN];
    size_t got = fread(magic, 1, sizeof(magic), handle);
    if (got != sizeof(magic) || memcmp(magic, RECIPE_MAGIC, RECIPE_MAGIC_LEN) != 0) {
        fclose(handle);
        err_set(e, "%s isnt a %s package.", name, RECIPE_EXTENSION);
        return 0;
    }
    if (fread(head, 1, sizeof(head), handle) != sizeof(head)) {
        fclose(handle);
        err_set(e, "%s is malformed: unpack requires a buffer of %d bytes", name, RECIPE_HEAD_LEN);
        return 0;
    }
    uint32_t version = read_u32le(head);
    int game_index = head[4];
    uint32_t header_size = read_u32le(head + 5);
    if (version != RECIPE_VERSION) {
        fclose(handle);
        err_set(e, "%s is format v%u, which this build cant pour.", name, version);
        return 0;
    }
    char *text = (char *)arena_alloc(&out->a, (size_t)header_size + 1);
    if (text == NULL) {
        fclose(handle);
        err_set(e, "%s is malformed: header too large", name);
        return 0;
    }
    size_t header_got = header_size > 0 ? fread(text, 1, header_size, handle) : 0;
    fclose(handle);
    text[header_got] = 0;

    err parse_err;
    err_clear(&parse_err);
    json_value *header = json_parse(&out->a, text, header_got, &parse_err);
    if (header == NULL || header->kind != JSON_OBJ) {
        err_set(e, "%s is malformed: %s", name, parse_err.set ? parse_err.text : "bad header");
        return 0;
    }
    const json_value *format = json_obj_get(header, "format");
    if (format == NULL || format->kind != JSON_STR || strcmp(format->text, RECIPE_FORMAT) != 0) {
        err_set(e, "%s isnt a %s package.", name, RECIPE_EXTENSION);
        return 0;
    }

    out->version = (int)version;
    out->game_index = game_index;
    out->header = header;
    out->data_start = RECIPE_MAGIC_LEN + RECIPE_HEAD_LEN + (int64_t)header_size;

    const json_value *entries = json_obj_get(header, "entries");
    size_t count = entries != NULL && entries->kind == JSON_ARR ? entries->count : 0;
    out->entries = (recipe_entry *)arena_alloc(&out->a, (count + 1) * sizeof(recipe_entry));
    if (out->entries == NULL) {
        err_set(e, "out of memory");
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        const json_value *raw = json_at(entries, i);
        const json_value *marker = json_obj_get(raw, "idx_marker");
        const json_value *offset = json_obj_get(raw, "entry_off");
        const json_value *size = json_obj_get(raw, "payload_size");
        const char *missing = marker == NULL ? "idx_marker"
                              : offset == NULL ? "entry_off"
                              : size == NULL ? "payload_size" : NULL;
        if (missing != NULL) {
            err_set(e, "%s is malformed: '%s'", name, missing);
            return 0;
        }
        recipe_entry *entry = &out->entries[i];
        entry->idx_marker = json_as_i64(marker, 0);
        entry->entry_off = json_as_i64(offset, 0);
        entry->comp_marker = json_as_i64(json_obj_get(raw, "comp_marker"), 0);
        entry->size = json_as_i64(size, 0);
        entry->digest = header_text(raw, "payload_sha256", "");
        entry->container = header_text(raw, "container", "");
        out->payload_bytes += entry->size;
    }
    out->entry_count = count;

    const char *title = header_text(header, "name", "");
    if (title[0] == 0) {
        char *stem = gs_path_stem(path);
        title = stem == NULL ? "" : arena_strdup(&out->a, stem, strlen(stem));
        free(stem);
    }
    out->name = title == NULL ? "" : title;
    out->game = header_text(header, "game", "");
    out->author = header_text(header, "author", "");
    out->version_text = header_text(header, "mod_version", "");
    out->description = header_text(header, "description", "");
    out->colour = header_text(header, "color", RECIPE_DEFAULT_COLOUR);

    const json_value *images = json_obj_get(header, "images");
    size_t image_count = images != NULL && images->kind == JSON_ARR ? images->count : 0;
    out->images = (recipe_asset *)arena_alloc(&out->a, (image_count + 1) * sizeof(recipe_asset));
    if (out->images == NULL) {
        err_set(e, "out of memory");
        return 0;
    }
    int64_t image_bytes = 0;
    for (size_t i = 0; i < image_count; i++) {
        read_asset(&out->a, json_at(images, i), &out->images[i]);
        image_bytes += out->images[i].size;
    }
    out->image_count = image_count;

    const json_value *audio = json_obj_get(header, "audio");
    out->has_audio = json_truthy(audio) && audio->kind == JSON_OBJ;
    read_asset(&out->a, out->has_audio ? audio : NULL, &out->audio);

    out->images_start = out->data_start + out->payload_bytes;
    out->audio_start = out->images_start + image_bytes;
    return 1;
}

void recipe_free(gs_recipe *r) {
    arena_free(&r->a);
    memset(r, 0, sizeof(*r));
}

int recipe_peek_game_index(const char *path) {
    FILE *handle = file_open(path, "rb");
    if (handle == NULL) {
        return -1;
    }
    unsigned char raw[RECIPE_MAGIC_LEN + RECIPE_HEAD_LEN];
    size_t got = fread(raw, 1, sizeof(raw), handle);
    fclose(handle);
    if (got < RECIPE_MAGIC_LEN || memcmp(raw, RECIPE_MAGIC, RECIPE_MAGIC_LEN) != 0) {
        return -1;
    }
    if (got != sizeof(raw)) {
        return -1;
    }
    if (read_u32le(raw + RECIPE_MAGIC_LEN) != RECIPE_VERSION) {
        return -1;
    }
    return raw[RECIPE_MAGIC_LEN + 4];
}

static int read_span(const char *path, int64_t offset, int64_t size, buf *out) {
    buf_reset(out);
    if (size < 0) {
        return 0;
    }
    FILE *handle = file_open(path, "rb");
    if (handle == NULL) {
        return -1;
    }
    if (_fseeki64(handle, offset, SEEK_SET) != 0) {
        fclose(handle);
        return 0;
    }
    if (!buf_reserve(out, (size_t)size)) {
        fclose(handle);
        return -1;
    }
    size_t got = size > 0 ? fread(out->data, 1, (size_t)size, handle) : 0;
    fclose(handle);
    out->len = got;
    out->data[got] = 0;
    return got == (size_t)size;
}

int recipe_read_payload(const gs_recipe *r, size_t index, buf *out, err *e) {
    const char *name = gs_path_name(r->path);
    if (index >= r->entry_count) {
        err_set(e, "%s has no payload %zu", name, index);
        return 0;
    }
    int64_t offset = r->data_start;
    for (size_t i = 0; i < index; i++) {
        offset += r->entries[i].size;
    }
    const recipe_entry *entry = &r->entries[index];
    int got = read_span(r->path, offset, entry->size, out);
    if (got < 0) {
        err_set(e, "Couldnt read %s", name);
        return 0;
    }
    if (got == 0) {
        err_set(e, "%s is truncated inside a payload.", name);
        return 0;
    }
    if (entry->digest != NULL && entry->digest[0] != 0) {
        char digest[65];
        sha256_bytes(out->data, out->len, digest);
        if (strcmp(digest, entry->digest) != 0) {
            err_set(e, "%s slot %lld is corrupt.", name, (long long)entry->entry_off);
            return 0;
        }
    }
    return 1;
}

int recipe_read_images(const gs_recipe *r, buf *blobs, err *e) {
    const char *name = gs_path_name(r->path);
    int64_t offset = r->images_start;
    for (size_t i = 0; i < r->image_count; i++) {
        int got = read_span(r->path, offset, r->images[i].size, &blobs[i]);
        if (got <= 0) {
            err_set(e, got < 0 ? "Couldnt read %s" : "%s is truncated inside a preview.", name);
            return 0;
        }
        offset += r->images[i].size;
    }
    return 1;
}

int recipe_read_audio(const gs_recipe *r, buf *out, int *present, err *e) {
    *present = 0;
    if (!r->has_audio) {
        return 1;
    }
    int got = read_span(r->path, r->audio_start, r->audio.size, out);
    if (got <= 0) {
        err_set(e, got < 0 ? "Couldnt read %s" : "%s is truncated inside the theme tune.",
                gs_path_name(r->path));
        return 0;
    }
    *present = 1;
    return 1;
}

static int has_extension(const char *name) {
    size_t len = strlen(name);
    size_t ext = strlen(RECIPE_EXTENSION);
    return len >= ext && _stricmp(name + len - ext, RECIPE_EXTENSION) == 0;
}

static int push_recipe(recipe_scan *s, gs_recipe *r) {
    gs_recipe *bigger = (gs_recipe *)realloc(s->items, (s->count + 1) * sizeof(gs_recipe));
    if (bigger == NULL) {
        return 0;
    }
    s->items = bigger;
    s->items[s->count++] = *r;
    return 1;
}

static int push_problem(recipe_scan *s, const char *path, const char *text) {
    char **paths = (char **)realloc(s->problem_paths, (s->problem_count + 1) * sizeof(char *));
    if (paths == NULL) {
        return 0;
    }
    s->problem_paths = paths;
    char **texts = (char **)realloc(s->problem_texts, (s->problem_count + 1) * sizeof(char *));
    if (texts == NULL) {
        return 0;
    }
    s->problem_texts = texts;
    s->problem_paths[s->problem_count] = _strdup(path);
    s->problem_texts[s->problem_count] = _strdup(text);
    s->problem_count++;
    return 1;
}

static int walk_folder(const char *folder, const char *game_id, int game_index, int keep_problems,
                       recipe_scan *out) {
    memset(out, 0, sizeof(*out));
    if (!path_is_dir(folder)) {
        return 1;
    }
    gs_names names;
    gs_names_init(&names);
    gs_list_dir(folder, &names);
    for (size_t i = 0; i < names.count; i++) {
        if (!has_extension(names.items[i])) {
            continue;
        }
        char *path = path_join(folder, names.items[i]);
        if (path == NULL) {
            continue;
        }
        int index = recipe_peek_game_index(path);
        if (keep_problems && index < 0) {
            push_problem(out, path, "Not a readable .gokon package");
            free(path);
            continue;
        }
        if (game_index >= 0 && index != game_index) {
            free(path);
            continue;
        }
        gs_recipe r;
        err e;
        err_clear(&e);
        if (!recipe_read(path, &r, &e)) {
            if (keep_problems) {
                push_problem(out, path, e.text);
            }
            recipe_free(&r);
            free(path);
            continue;
        }
        free(path);
        if (game_id != NULL && game_id[0] != 0 && strcmp(r.game, game_id) != 0) {
            recipe_free(&r);
            continue;
        }
        if (!push_recipe(out, &r)) {
            recipe_free(&r);
        }
    }
    gs_names_free(&names);
    return 1;
}

int recipe_scan_folder(const char *folder, const char *game_id, int game_index, recipe_scan *out) {
    return walk_folder(folder, game_id, game_index, 1, out);
}

int recipe_list(const char *folder, const char *game_id, int game_index, recipe_scan *out) {
    return walk_folder(folder, game_id, game_index, 0, out);
}

void recipe_scan_free(recipe_scan *s) {
    for (size_t i = 0; i < s->count; i++) {
        recipe_free(&s->items[i]);
    }
    for (size_t i = 0; i < s->problem_count; i++) {
        free(s->problem_paths[i]);
        free(s->problem_texts[i]);
    }
    free(s->items);
    free(s->problem_paths);
    free(s->problem_texts);
    memset(s, 0, sizeof(*s));
}

static uint64_t hash_text(const char *text, size_t len) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)text[i];
        h *= 1099511628211ull;
    }
    return h;
}

typedef struct {
    const char **slots;
    size_t mask;
} record_set;

static int record_set_build(record_set *set, const char *const *records, size_t count, size_t room) {
    size_t cap = 16;
    while (cap < room * 2 + 2) {
        cap <<= 1;
    }
    set->slots = (const char **)calloc(cap, sizeof(const char *));
    set->mask = cap - 1;
    if (set->slots == NULL) {
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        size_t at = (size_t)hash_text(records[i], strlen(records[i])) & set->mask;
        while (set->slots[at] != NULL && strcmp(set->slots[at], records[i]) != 0) {
            at = (at + 1) & set->mask;
        }
        set->slots[at] = records[i];
    }
    return 1;
}

static const char *record_set_find(const record_set *set, const char *key, size_t len) {
    size_t at = (size_t)hash_text(key, len) & set->mask;
    while (set->slots[at] != NULL) {
        if (strlen(set->slots[at]) == len && memcmp(set->slots[at], key, len) == 0) {
            return set->slots[at];
        }
        at = (at + 1) & set->mask;
    }
    return NULL;
}

int recipe_collect_sources(const char *source_folder, const char *const *records, size_t record_count,
                           recipe_match **out, size_t *count) {
    *out = NULL;
    *count = 0;
    char *root = gs_path_full(source_folder);
    if (root == NULL) {
        return 0;
    }
    gs_names files;
    gs_names_init(&files);
    gs_list_tree_files(root, &files);

    record_set set;
    if (!record_set_build(&set, records, record_count, record_count)) {
        gs_names_free(&files);
        free(root);
        return 0;
    }
    record_set seen;
    if (!record_set_build(&seen, NULL, 0, files.count)) {
        free(set.slots);
        gs_names_free(&files);
        free(root);
        return 0;
    }

    recipe_match *matches = (recipe_match *)calloc(files.count + 1, sizeof(recipe_match));
    size_t found = 0;
    for (size_t i = 0; matches != NULL && i < files.count; i++) {
        if (has_extension(files.items[i])) {
            continue;
        }
        char *full = path_join(root, files.items[i]);
        if (full == NULL) {
            continue;
        }
        size_t len = strlen(full);
        size_t root_len = (len >= 3 && full[1] == ':' && full[2] == '\\') ? 3 : 0;
        const char *parts[512];
        size_t part_lens[512];
        size_t part_count = 0;
        if (root_len > 0) {
            parts[part_count] = full;
            part_lens[part_count++] = root_len;
        }
        for (size_t k = root_len; k < len && part_count < 512;) {
            size_t start = k;
            while (k < len && full[k] != '\\') {
                k++;
            }
            if (k > start) {
                parts[part_count] = full + start;
                part_lens[part_count++] = k - start;
            }
            k++;
        }
        char *joined = (char *)malloc(len + 16);
        if (joined != NULL) {
            int depth_max = part_count < 12 ? (int)part_count : 12;
            for (int depth = depth_max; depth > 0; depth--) {
                size_t at = 0;
                for (size_t p = part_count - (size_t)depth; p < part_count; p++) {
                    if (at > 0) {
                        joined[at++] = '/';
                    }
                    memcpy(joined + at, parts[p], part_lens[p]);
                    at += part_lens[p];
                }
                joined[at] = 0;
                const char *key = record_set_find(&set, joined, at);
                if (key != NULL && record_set_find(&seen, key, strlen(key)) == NULL) {
                    size_t slot = (size_t)hash_text(key, strlen(key)) & seen.mask;
                    while (seen.slots[slot] != NULL) {
                        slot = (slot + 1) & seen.mask;
                    }
                    seen.slots[slot] = key;
                    matches[found].key = _strdup(key);
                    matches[found].path = _strdup(full);
                    found++;
                    break;
                }
            }
            free(joined);
        }
        free(full);
    }
    free(seen.slots);
    free(set.slots);
    gs_names_free(&files);
    free(root);
    *out = matches;
    *count = found;
    return matches != NULL;
}

void recipe_matches_free(recipe_match *matches, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(matches[i].key);
        free(matches[i].path);
    }
    free(matches);
}
