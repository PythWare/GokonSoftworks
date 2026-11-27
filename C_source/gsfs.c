#define WIN32_LEAN_AND_MEAN
#include "gsfs.h"
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static char package_root[MAX_PATH * 4];
static char project_root[MAX_PATH * 4];
static int roots_ready;
static SRWLOCK log_lock = SRWLOCK_INIT;
static gs_log_sink log_sink;
static void *log_sink_ctx;

static void trim_to_parent(char *path) {
    size_t len = strlen(path);
    while (len > 0 && path[len - 1] != '\\' && path[len - 1] != '/') {
        len--;
    }
    while (len > 1 && (path[len - 1] == '\\' || path[len - 1] == '/') && path[len - 2] != ':') {
        len--;
    }
    path[len] = 0;
}

static void ensure_roots(void) {
    if (roots_ready) {
        return;
    }
    wchar_t wide[MAX_PATH * 2];
    DWORD got = GetModuleFileNameW(NULL, wide, (DWORD)(sizeof(wide) / sizeof(wide[0])));
    package_root[0] = 0;
    if (got > 0 && got < sizeof(wide) / sizeof(wide[0])) {
        char *utf8 = wide_to_utf8(wide);
        if (utf8 != NULL) {
            snprintf(package_root, sizeof(package_root), "%s", utf8);
            free(utf8);
            trim_to_parent(package_root);
        }
    }
    snprintf(project_root, sizeof(project_root), "%s", package_root);
    trim_to_parent(project_root);
    roots_ready = 1;
}

void gs_set_package_root(const char *path) {
    snprintf(package_root, sizeof(package_root), "%s", path);
    size_t len = strlen(package_root);
    while (len > 3 && (package_root[len - 1] == '\\' || package_root[len - 1] == '/')) {
        package_root[--len] = 0;
    }
    snprintf(project_root, sizeof(project_root), "%s", package_root);
    trim_to_parent(project_root);
    roots_ready = 1;
}

const char *gs_package_root(void) {
    ensure_roots();
    return package_root;
}

const char *gs_project_root(void) {
    ensure_roots();
    return project_root;
}

static char *made_dir(const char *base, const char *tail) {
    char *path = path_join(base, tail);
    if (path != NULL) {
        path_make_dirs(path);
    }
    return path;
}

char *gs_png_dir(void) {
    return path_join(gs_package_root(), "pngs");
}

char *gs_taildata_dir(void) {
    return made_dir(gs_package_root(), "Taildata");
}

char *gs_filenames_dir(void) {
    return made_dir(gs_package_root(), "Filenames");
}

char *gs_mods_dir(void) {
    return made_dir(gs_project_root(), "Mods");
}

char *gs_log_path(void) {
    return path_join(gs_project_root(), "gokonsoftworks.log");
}

char *gs_taildata_path(const char *game_id) {
    char *dir = gs_taildata_dir();
    if (dir == NULL) {
        return NULL;
    }
    char name[300];
    snprintf(name, sizeof(name), "gokon_taildata_%s.json", game_id);
    char *path = path_join(dir, name);
    free(dir);
    return path;
}

void gs_log_set_sink(gs_log_sink sink, void *ctx) {
    AcquireSRWLockExclusive(&log_lock);
    log_sink = sink;
    log_sink_ctx = ctx;
    ReleaseSRWLockExclusive(&log_lock);
}

void gs_log(const char *level, const char *fmt, ...) {
    char text[ERR_MAX * 2];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);

    SYSTEMTIME now;
    GetLocalTime(&now);
    char line[ERR_MAX * 2 + 96];
    int len = snprintf(line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u,%03u %s gokonsoftworks: %s\r\n",
                       now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
                       now.wMilliseconds, level, text);
    if (len < 0) {
        return;
    }
    if ((size_t)len >= sizeof(line)) {
        len = (int)sizeof(line) - 1;
    }

    AcquireSRWLockExclusive(&log_lock);
    char *path = gs_log_path();
    FILE *sink = path == NULL ? NULL : file_open(path, "ab");
    free(path);
    if (sink != NULL) {
        fwrite(line, 1, (size_t)len, sink);
        fclose(sink);
    } else {
        fwrite(line, 1, (size_t)len, stderr);
    }
    if (log_sink != NULL) {
        log_sink(log_sink_ctx, level, text);
    }
    ReleaseSRWLockExclusive(&log_lock);
}

void gs_human_size(int64_t size, char *out, size_t room) {
    static const char *const units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = (double)size;
    for (int i = 0; i < 5; i++) {
        if (value < 1024.0 || i == 4) {
            if (i == 0) {
                snprintf(out, room, "%.0f %s", value, units[i]);
            } else {
                snprintf(out, room, "%.1f %s", value, units[i]);
            }
            return;
        }
        value /= 1024.0;
    }
}

void gs_format_bytes(int64_t size, int known, char *out, size_t room) {
    static const char *const units[] = {"B", "KB", "MB", "GB", "TB"};
    if (!known) {
        snprintf(out, room, "Unknown");
        return;
    }
    const char *sign = size < 0 ? "-" : "";
    double value = size < 0 ? -(double)size : (double)size;
    for (int i = 0; i < 5; i++) {
        if (value < 1000.0 || i == 4) {
            if (i == 0) {
                snprintf(out, room, "%s%lld %s", sign, (long long)value, units[i]);
                return;
            }
            char digits[64];
            snprintf(digits, sizeof(digits), "%.2f", value);
            size_t len = strlen(digits);
            while (len > 0 && digits[len - 1] == '0') {
                digits[--len] = 0;
            }
            while (len > 0 && digits[len - 1] == '.') {
                digits[--len] = 0;
            }
            snprintf(out, room, "%s%s %s", sign, digits, units[i]);
            return;
        }
        value /= 1000.0;
    }
}

int64_t gs_align_up(int64_t value, int64_t alignment) {
    return (value + (alignment - 1)) & ~(alignment - 1);
}

static size_t drive_length(const char *s, size_t len) {
    if (len >= 2 && s[0] == '\\' && s[1] == '\\') {
        size_t start = 2;
        if (len >= 8 && _strnicmp(s, "\\\\?\\UNC\\", 8) == 0) {
            start = 8;
        }
        const char *first = start < len ? strchr(s + start, '\\') : NULL;
        if (first == NULL) {
            return len;
        }
        const char *second = strchr(first + 1, '\\');
        if (second == NULL) {
            return len;
        }
        return (size_t)(second - s);
    }
    if (len >= 2 && s[1] == ':') {
        return 2;
    }
    return 0;
}

char *gs_path_normalize(const char *path) {
    size_t len = strlen(path);
    char *s = (char *)malloc(len + 1);
    char *out = (char *)malloc(len + 2);
    if (s == NULL || out == NULL) {
        free(s);
        free(out);
        return NULL;
    }
    memcpy(s, path, len + 1);
    path_to_backslash(s);
    size_t drive = drive_length(s, len);
    size_t at = 0;
    memcpy(out, s, drive);
    at = drive;
    size_t i = drive;
    if (i < len && s[i] == '\\') {
        out[at++] = '\\';
        i++;
    }
    size_t base = at;
    while (i < len) {
        size_t start = i;
        while (i < len && s[i] != '\\') {
            i++;
        }
        size_t part = i - start;
        if (part > 0 && !(part == 1 && s[start] == '.')) {
            if (at > base) {
                out[at++] = '\\';
            }
            memcpy(out + at, s + start, part);
            at += part;
        }
        i++;
    }
    if (at == 0) {
        out[at++] = '.';
    }
    out[at] = 0;
    free(s);
    return out;
}

static wchar_t *lower_wide(const char *text) {
    wchar_t *wide = utf8_to_wide(text == NULL ? "" : text);
    if (wide != NULL) {
        size_t len = wcslen(wide);
        if (len > 0) {
            CharLowerBuffW(wide, (DWORD)len);
        }
    }
    return wide;
}

int gs_casefold_compare(const char *a, const char *b) {
    wchar_t *wa = lower_wide(a);
    wchar_t *wb = lower_wide(b);
    int result = 0;
    if (wa != NULL && wb != NULL) {
        result = wcscmp(wa, wb);
    } else {
        result = strcmp(a == NULL ? "" : a, b == NULL ? "" : b);
    }
    free(wa);
    free(wb);
    return result;
}

int gs_casefold_equal(const char *a, const char *b) {
    return gs_casefold_compare(a, b) == 0;
}

int gs_path_equal(const char *a, const char *b) {
    char *na = gs_path_normalize(a);
    char *nb = gs_path_normalize(b);
    int same = na != NULL && nb != NULL && gs_casefold_equal(na, nb);
    free(na);
    free(nb);
    return same;
}

const char *gs_path_name(const char *path) {
    const char *slash = strrchr(path, '\\');
    const char *alt = strrchr(path, '/');
    if (alt != NULL && (slash == NULL || alt > slash)) {
        slash = alt;
    }
    const char *name = slash == NULL ? path : slash + 1;
    if (slash == NULL && strlen(path) >= 2 && path[1] == ':') {
        name = path + 2;
    }
    return name;
}

char *gs_path_stem(const char *path) {
    const char *name = gs_path_name(path);
    const char *dot = strrchr(name, '.');
    size_t len = (dot != NULL && dot > name) ? (size_t)(dot - name) : strlen(name);
    char *out = (char *)malloc(len + 1);
    if (out != NULL) {
        memcpy(out, name, len);
        out[len] = 0;
    }
    return out;
}

char *gs_path_full(const char *path) {
    wchar_t *wide = utf8_to_wide(path);
    if (wide == NULL) {
        return NULL;
    }
    for (wchar_t *c = wide; *c; c++) {
        if (*c == L'/') {
            *c = L'\\';
        }
    }
    DWORD need = GetFullPathNameW(wide, 0, NULL, NULL);
    if (need == 0) {
        char *same = wide_to_utf8(wide);
        free(wide);
        return same;
    }
    wchar_t *full = (wchar_t *)malloc((size_t)need * sizeof(wchar_t));
    if (full == NULL) {
        free(wide);
        return NULL;
    }
    DWORD got = GetFullPathNameW(wide, need, full, NULL);
    free(wide);
    char *out = (got > 0 && got < need) ? wide_to_utf8(full) : NULL;
    free(full);
    if (out != NULL) {
        size_t len = strlen(out);
        while (len > 3 && out[len - 1] == '\\') {
            out[--len] = 0;
        }
    }
    return out;
}

char *gs_path_parent(const char *path) {
    size_t len = strlen(path);
    char *out = (char *)malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, path, len + 1);
    size_t drive = drive_length(out, len);
    size_t cut = len;
    while (cut > drive && out[cut - 1] != '\\' && out[cut - 1] != '/') {
        cut--;
    }
    if (cut > drive + 1) {
        cut--;
    }
    if (cut <= drive) {
        cut = len;
    }
    out[cut] = 0;
    return out;
}

void gs_names_init(gs_names *list) {
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

int gs_names_push(gs_names *list, const char *text) {
    if (list->count == list->cap) {
        size_t grown = list->cap == 0 ? 16 : list->cap * 2;
        char **bigger = (char **)realloc(list->items, grown * sizeof(char *));
        if (bigger == NULL) {
            return 0;
        }
        list->items = bigger;
        list->cap = grown;
    }
    char *copy = _strdup(text);
    if (copy == NULL) {
        return 0;
    }
    list->items[list->count++] = copy;
    return 1;
}

void gs_names_free(gs_names *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
    gs_names_init(list);
}

typedef struct {
    wchar_t *key;
    char *text;
} sort_item;

static int compare_items(const void *a, const void *b) {
    const sort_item *x = (const sort_item *)a;
    const sort_item *y = (const sort_item *)b;
    return wcscmp(x->key, y->key);
}

void gs_names_sort_paths(gs_names *list) {
    if (list->count < 2) {
        return;
    }
    sort_item *items = (sort_item *)calloc(list->count, sizeof(sort_item));
    if (items == NULL) {
        return;
    }
    int ok = 1;
    for (size_t i = 0; i < list->count; i++) {
        items[i].text = list->items[i];
        items[i].key = lower_wide(list->items[i]);
        if (items[i].key == NULL) {
            ok = 0;
            continue;
        }
        for (wchar_t *c = items[i].key; *c; c++) {
            if (*c == L'\\' || *c == L'/') {
                *c = 1;
            }
        }
    }
    if (ok) {
        qsort(items, list->count, sizeof(sort_item), compare_items);
        for (size_t i = 0; i < list->count; i++) {
            list->items[i] = items[i].text;
        }
    }
    for (size_t i = 0; i < list->count; i++) {
        free(items[i].key);
    }
    free(items);
}

static int list_into(const char *folder, const char *prefix, int recurse, gs_names *out) {
    char *pattern = path_join(folder, "*");
    if (pattern == NULL) {
        return 0;
    }
    wchar_t *wide = path_to_wide(pattern);
    free(pattern);
    if (wide == NULL) {
        return 0;
    }
    WIN32_FIND_DATAW found;
    HANDLE handle = FindFirstFileExW(wide, FindExInfoBasic, &found, FindExSearchNameMatch,
                                     NULL, FIND_FIRST_EX_LARGE_FETCH);
    free(wide);
    if (handle == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    int ok = 1;
    do {
        if (wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0) {
            continue;
        }
        char *name = wide_to_utf8(found.cFileName);
        if (name == NULL) {
            continue;
        }
        char *relative = prefix == NULL ? _strdup(name) : path_join(prefix, name);
        int is_dir = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        int is_link = (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        if (relative != NULL) {
            if (!recurse) {
                ok = gs_names_push(out, relative);
            } else if (is_dir) {
                if (!is_link) {
                    char *child = path_join(folder, name);
                    if (child != NULL) {
                        list_into(child, relative, 1, out);
                        free(child);
                    }
                }
            } else {
                ok = gs_names_push(out, relative);
            }
            free(relative);
        }
        free(name);
    } while (ok && FindNextFileW(handle, &found));
    FindClose(handle);
    return ok;
}

int gs_list_dir(const char *folder, gs_names *out) {
    if (!path_is_dir(folder)) {
        return 0;
    }
    int ok = list_into(folder, NULL, 0, out);
    gs_names_sort_paths(out);
    return ok;
}

int gs_list_tree_files(const char *folder, gs_names *out) {
    if (!path_is_dir(folder)) {
        return 0;
    }
    int ok = list_into(folder, NULL, 1, out);
    gs_names_sort_paths(out);
    return ok;
}

static int is_region_name(const char *base) {
    size_t len = strlen(base);
    return len >= 13 && _strnicmp(base, "LINKDATA_", 9) == 0 &&
           _stricmp(base + len - 4, ".BIN") == 0;
}

int gs_wants_region_pair(const char *name) {
    return is_region_name(gs_path_name(name));
}

int gs_region_pair(const char *folder, char **bin_out, char **toc_out) {
    *bin_out = NULL;
    *toc_out = NULL;
    gs_names names;
    gs_names_init(&names);
    if (!gs_list_dir(folder, &names)) {
        gs_names_free(&names);
        return 0;
    }
    int hit = 0;
    for (size_t i = 0; i < names.count && !hit; i++) {
        const char *name = names.items[i];
        size_t len = strlen(name);
        if (len < 4 || _strnicmp(name, "LINKDATA_", 9) != 0 || _stricmp(name + len - 4, ".BIN") != 0) {
            continue;
        }
        char *path = path_join(folder, name);
        int is_file = path != NULL && path_is_file(path);
        free(path);
        if (!is_file) {
            continue;
        }
        char *toc = (char *)malloc(len + 1);
        if (toc == NULL) {
            break;
        }
        memcpy(toc, name, len - 4);
        memcpy(toc + len - 4, ".IDX", 5);
        char *toc_path = path_join(folder, toc);
        int toc_ok = toc_path != NULL && path_is_file(toc_path);
        free(toc_path);
        if (toc_ok) {
            *bin_out = _strdup(name);
            *toc_out = toc;
            hit = *bin_out != NULL;
        } else {
            free(toc);
        }
    }
    gs_names_free(&names);
    return hit;
}
