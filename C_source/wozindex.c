#include "wozindex.h"
#include "repack.h"
#include "sebank.h"
#include "subfolder.h"
#include <stdlib.h>
#include <string.h>

static size_t be32(const buf *b, size_t at) {
    const unsigned char *p = (const unsigned char *)b->data + at;
    return ((size_t)p[0] << 24) | ((size_t)p[1] << 16) | ((size_t)p[2] << 8) | (size_t)p[3];
}

static const char *base_name(const char *path) {
    const char *back = strrchr(path, '\\');
    const char *fwd = strrchr(path, '/');
    const char *cut = back > fwd ? back : fwd;
    return cut != NULL ? cut + 1 : path;
}

static char *beside(const char *path, const char *name) {
    size_t dir_len = (size_t)(base_name(path) - path);
    size_t name_len = strlen(name);
    char *out = (char *)malloc(dir_len + name_len + 1);
    if (out != NULL) {
        memcpy(out, path, dir_len);
        memcpy(out + dir_len, name, name_len + 1);
    }
    return out;
}

static int index_shape(const buf *chunk, int terminated, size_t *count) {
    if (chunk->len < 8 || be32(chunk, 4) != 0) {
        return 0;
    }
    size_t c = be32(chunk, 0);
    size_t need = 8 + c * 8 + (terminated ? 8 : 0);
    if (chunk->len != need) {
        return 0;
    }
    *count = c;
    return 1;
}

static int has_nested_folder(const char *path) {
    int found = 0;
    for (int own = 0; own < 2 && !found; own++) {
        char *folder = subfolder_for(path, own);
        found = folder != NULL && path_is_dir(folder);
        free(folder);
    }
    return found;
}

static int replace_chunk(buf *chunk, const size_t *lens, size_t count, int terminated, err *e) {
    buf index;
    buf_init(&index);
    int ok = sebank_index(lens, count, terminated, &index, e);
    if (ok) {
        buf_reset(chunk);
        ok = buf_put(chunk, index.data, index.len);
        if (!ok) {
            err_set(e, "out of memory replacing a stream index");
        }
    }
    buf_free(&index);
    return ok;
}

static int refresh_se_index(const char *source, buf *chunk, err *e) {
    size_t count = 0;
    if (!index_shape(chunk, 1, &count)) {
        return 1;
    }
    char *banks_path = beside(source, "se.bin");
    if (banks_path == NULL || !has_nested_folder(banks_path)) {
        free(banks_path);
        return 1;
    }
    buf banks;
    buf_init(&banks);
    int ok = repack_read_chunk(banks_path, &banks, e);
    arena a;
    arena_init(&a);
    sebank_layout layout;
    if (ok && !sebank_read((const unsigned char *)banks.data, banks.len, &a, &layout)) {
        err_set(e, "the rebuilt %s no longer holds .wbh/.wbd sound banks", banks_path);
        ok = 0;
    }
    if (ok && layout.count != count) {
        err_set(e, "the rebuilt %s holds %u sound bank files but seidx.bin indexes %zu, keep the bank count", banks_path,
                layout.count, count);
        ok = 0;
    }
    if (ok) {
        ok = replace_chunk(chunk, layout.sizes, count, 1, e);
    }
    arena_free(&a);
    buf_free(&banks);
    free(banks_path);
    return ok;
}

static int refresh_voice_index(const char *part_folder, buf *chunk, err *e) {
    size_t count = 0;
    if (!index_shape(chunk, 0, &count)) {
        return 1;
    }
    char *trimmed = _strdup(part_folder);
    size_t len = trimmed != NULL ? strlen(trimmed) : 0;
    while (len > 0 && (trimmed[len - 1] == '\\' || trimmed[len - 1] == '/')) {
        trimmed[--len] = '\0';
    }
    char *voice_dir = trimmed != NULL ? beside(trimmed, "Voice") : NULL;
    free(trimmed);
    if (voice_dir == NULL || !path_is_dir(voice_dir)) {
        free(voice_dir);
        return 1;
    }
    char **files = NULL;
    size_t found = 0;
    int ok = repack_list_sorted(voice_dir, &files, &found, e);
    if (ok && found != count) {
        err_set(e, "%s holds %zu voice streams but voiceidx.bin indexes %zu, keep one file per stream", voice_dir, found,
                count);
        ok = 0;
    }
    size_t *lens = ok ? (size_t *)calloc(count > 0 ? count : 1, sizeof(size_t)) : NULL;
    if (ok && lens == NULL) {
        err_set(e, "out of memory sizing the voice streams");
        ok = 0;
    }
    for (size_t i = 0; ok && i < count; i++) {
        int64_t size = path_file_size(files[i]);
        if (size < 0) {
            err_set(e, "couldnt size %s", files[i]);
            ok = 0;
        } else {
            lens[i] = (size_t)size;
        }
    }
    if (ok) {
        ok = replace_chunk(chunk, lens, count, 0, e);
    }
    free(lens);
    if (files != NULL) {
        repack_free_sorted(files, found);
    }
    free(voice_dir);
    return ok;
}

int wozindex_refresh(const char *part_folder, const char *source, buf *chunk, err *e) {
    const char *name = base_name(source);
    if (_stricmp(name, "seidx.bin") == 0) {
        return refresh_se_index(source, chunk, e);
    }
    if (_stricmp(name, "voiceidx.bin") == 0) {
        return refresh_voice_index(part_folder, chunk, e);
    }
    return 1;
}
