#include "subfolder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *last_sep(const char *path) {
    const char *a = strrchr(path, '\\');
    const char *b = strrchr(path, '/');
    return a > b ? a : b;
}

static const char *ext_dot(const char *path) {
    const char *sep = last_sep(path);
    const char *dot = strrchr(path, '.');
    return dot != NULL && (sep == NULL || dot > sep) ? dot : NULL;
}

char *subfolder_for(const char *file_path, int own) {
    const char *dot = ext_dot(file_path);
    size_t stem = dot == NULL ? strlen(file_path) : (size_t)(dot - file_path);
    const char *ext = dot == NULL ? "" : dot + 1;
    size_t room = stem + (own ? strlen(ext) + 2 : 1);
    char *out = (char *)malloc(room);
    if (out == NULL) {
        return NULL;
    }
    if (own) {
        snprintf(out, room, "%.*s_%s", (int)stem, file_path, ext);
    } else {
        snprintf(out, room, "%.*s", (int)stem, file_path);
    }
    return out;
}

char *subfolder_pseudo_path(const char *file_path) {
    char *folder = subfolder_for(file_path, 1);
    if (folder == NULL) {
        return NULL;
    }
    size_t room = strlen(folder) + 5;
    char *out = (char *)malloc(room);
    if (out != NULL) {
        snprintf(out, room, "%s.sub", folder);
    }
    free(folder);
    return out;
}

int subfolder_matches(const char *folder_name, const char *file_name) {
    char *plain = subfolder_for(file_name, 0);
    char *own = subfolder_for(file_name, 1);
    int hit = (plain != NULL && strcmp(plain, folder_name) == 0) ||
              (own != NULL && strcmp(own, folder_name) == 0);
    free(plain);
    free(own);
    return hit;
}

static uint64_t key_hash(const char *text, size_t len) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 'A' && c <= 'Z') {
            c = (unsigned char)(c - 'A' + 'a');
        }
        if (c == '/') {
            c = '\\';
        }
        h = (h ^ c) * 1099511628211ULL;
    }
    return h;
}

static int key_equal(const char *a, size_t alen, const char *b, size_t blen) {
    if (alen != blen) {
        return 0;
    }
    for (size_t i = 0; i < alen; i++) {
        unsigned char x = (unsigned char)a[i];
        unsigned char y = (unsigned char)b[i];
        if (x >= 'A' && x <= 'Z') {
            x = (unsigned char)(x - 'A' + 'a');
        }
        if (y >= 'A' && y <= 'Z') {
            y = (unsigned char)(y - 'A' + 'a');
        }
        if (x == '/') {
            x = '\\';
        }
        if (y == '/') {
            y = '\\';
        }
        if (x != y) {
            return 0;
        }
    }
    return 1;
}

typedef struct {
    const char *text;
    size_t len;
    int64_t owner;
    int folder;
} stem_slot;

static stem_slot *slot_find(stem_slot *table, size_t slots, const char *text, size_t len) {
    size_t at = (size_t)(key_hash(text, len) & (slots - 1));
    while (table[at].text != NULL && !key_equal(table[at].text, table[at].len, text, len)) {
        at = (at + 1) & (slots - 1);
    }
    return &table[at];
}

int subfolder_mark_shared(char *const *rel, int64_t count, int *own) {
    size_t keys = 0;
    for (int64_t i = 0; i < count; i++) {
        own[i] = 0;
        if (rel[i] == NULL) {
            continue;
        }
        keys++;
        for (const char *p = rel[i]; *p; p++) {
            if (*p == '\\' || *p == '/') {
                keys++;
            }
        }
    }
    size_t slots = 16;
    while (slots < keys * 2 + 2) {
        slots *= 2;
    }
    stem_slot *table = (stem_slot *)calloc(slots, sizeof(stem_slot));
    if (table == NULL) {
        return 0;
    }

    for (int64_t i = 0; i < count; i++) {
        if (rel[i] == NULL) {
            continue;
        }
        for (const char *p = rel[i]; *p; p++) {
            if (*p != '\\' && *p != '/') {
                continue;
            }
            stem_slot *slot = slot_find(table, slots, rel[i], (size_t)(p - rel[i]));
            if (slot->text == NULL) {
                slot->text = rel[i];
                slot->len = (size_t)(p - rel[i]);
                slot->owner = -1;
            }
            slot->folder = 1;
        }
    }

    for (int64_t i = 0; i < count; i++) {
        if (rel[i] == NULL) {
            continue;
        }
        const char *dot = ext_dot(rel[i]);
        size_t stem = dot == NULL ? strlen(rel[i]) : (size_t)(dot - rel[i]);
        stem_slot *slot = slot_find(table, slots, rel[i], stem);
        if (slot->text == NULL) {
            slot->text = rel[i];
            slot->len = stem;
            slot->owner = i;
            continue;
        }
        own[i] = 1;
        if (slot->owner >= 0) {
            own[slot->owner] = 1;
        }
    }

    free(table);
    return 1;
}
