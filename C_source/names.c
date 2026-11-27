#include "names.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void name_list_free(name_list *list) {
    free(list->items);
    free(list->backing);
    memset(list, 0, sizeof(*list));
}

int name_list_load(name_list *list, const char *ref_dir, const char *game_id,
                   const char *pack) {
    memset(list, 0, sizeof(*list));
    if (ref_dir == NULL) {
        return 0;
    }

    buf raw;
    buf_init(&raw);
    int read = 0;
    for (int attempt = 0; attempt < 2 && !read; attempt++) {
        char file[96];
        if (attempt == 0) {
            if (pack == NULL) {
                continue;
            }
            snprintf(file, sizeof(file), "%s_%s.ref", game_id, pack);
        } else {
            snprintf(file, sizeof(file), "%s.ref", game_id);
        }
        char *path = path_join(ref_dir, file);
        if (path == NULL) {
            break;
        }
        read = file_read_all(path, &raw);
        free(path);
    }
    if (!read) {
        buf_free(&raw);
        return 0;
    }

    int64_t lines = 0;
    for (size_t i = 0; i < raw.len; i++) {
        if (raw.data[i] == '\n') {
            lines++;
        }
    }
    if (raw.len > 0 && raw.data[raw.len - 1] != '\n') {
        lines++;
    }
    if (lines == 0) {
        buf_free(&raw);
        return 0;
    }

    list->items = (char **)calloc((size_t)lines, sizeof(char *));
    if (list->items == NULL) {
        buf_free(&raw);
        return 0;
    }
    list->backing = raw.data;

    char *cursor = list->backing;
    char *end = list->backing + raw.len;
    while (cursor < end && list->count < lines) {
        char *stop = cursor;
        while (stop < end && *stop != '\n') {
            stop++;
        }
        char *trim = stop;
        while (trim > cursor && (trim[-1] == '\r' || trim[-1] == ' ' || trim[-1] == '\t')) {
            trim--;
        }
        *trim = 0;
        list->items[list->count++] = cursor;
        cursor = stop + 1;
    }
    if (list->count == 0) {
        name_list_free(list);
        return 0;
    }
    return 1;
}

const char *name_at(const name_list *list, int64_t index) {
    if (list == NULL || list->items == NULL || index < 0 || index >= list->count) {
        return NULL;
    }
    const char *text = list->items[index];
    return (text != NULL && text[0] != 0) ? text : NULL;
}

static uint64_t name_hash(const char *text) {
    uint64_t h = 1469598103934665603ULL;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        unsigned char c = (*p >= 'A' && *p <= 'Z') ? (unsigned char)(*p - 'A' + 'a') : *p;
        h = (h ^ c) * 1099511628211ULL;
    }
    return h;
}

int names_make_unique(char **rel, const int64_t *slot, int64_t count, err *e) {
    size_t slots = 16;
    while (slots < (size_t)count * 2) {
        slots *= 2;
    }
    int64_t *table = (int64_t *)malloc(sizeof(int64_t) * slots);
    if (table == NULL) {
        err_set(e, "out of memory checking for repeated names");
        return 0;
    }
    for (size_t i = 0; i < slots; i++) {
        table[i] = -1;
    }

    int ok = 1;
    for (int64_t i = 0; i < count && ok; i++) {
        if (rel[i] == NULL) {
            continue;
        }
        size_t at = (size_t)(name_hash(rel[i]) & (slots - 1));
        int taken = 0;
        while (table[at] >= 0) {
            if (_stricmp(rel[table[at]], rel[i]) == 0) {
                taken = 1;
                break;
            }
            at = (at + 1) & (slots - 1);
        }
        if (!taken) {
            table[at] = i;
            continue;
        }

        const char *dot = strrchr(rel[i], '.');
        const char *slash = strrchr(rel[i], '\\');
        if (dot != NULL && slash != NULL && dot < slash) {
            dot = NULL;
        }
        size_t stem = dot == NULL ? strlen(rel[i]) : (size_t)(dot - rel[i]);
        size_t room = strlen(rel[i]) + 24;
        char *unique = (char *)malloc(room);
        if (unique == NULL) {
            err_set(e, "out of memory renaming a repeated entry");
            ok = 0;
            break;
        }
        snprintf(unique, room, "%.*s__%05lld%s", (int)stem, rel[i],
                 (long long)slot[i], dot == NULL ? "" : dot);
        free(rel[i]);
        rel[i] = unique;

        at = (size_t)(name_hash(rel[i]) & (slots - 1));
        while (table[at] >= 0) {
            at = (at + 1) & (slots - 1);
        }
        table[at] = i;
    }

    free(table);
    return ok;
}
