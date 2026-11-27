#define WIN32_LEAN_AND_MEAN
#include "util.h"
#include <windows.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void arena_init(arena *a) {
    a->head = NULL;
}

void arena_reset(arena *a) {
    arena_block *block = a->head;
    if (block == NULL) {
        return;
    }
    arena_block *next = block->next;
    while (next != NULL) {
        arena_block *after = next->next;
        free(next);
        next = after;
    }
    block->next = NULL;
    block->used = 0;
}

void *arena_alloc(arena *a, size_t size) {
    size = (size + 15u) & ~(size_t)15u;
    arena_block *block = a->head;
    if (block == NULL || block->cap - block->used < size) {
        size_t want = size + sizeof(arena_block);
        size_t cap = want > ARENA_BLOCK_MIN ? want : ARENA_BLOCK_MIN;
        block = (arena_block *)malloc(cap);
        if (block == NULL) {
            return NULL;
        }
        block->next = a->head;
        block->used = 0;
        block->cap = cap - sizeof(arena_block);
        a->head = block;
    }
    void *out = (unsigned char *)(block + 1) + block->used;
    block->used += size;
    return out;
}

char *arena_strdup(arena *a, const char *text, size_t len) {
    char *out = (char *)arena_alloc(a, len + 1);
    if (out == NULL) {
        return NULL;
    }
    if (len > 0) {
        memcpy(out, text, len);
    }
    out[len] = 0;
    return out;
}

void arena_free(arena *a) {
    arena_block *block = a->head;
    while (block != NULL) {
        arena_block *next = block->next;
        free(block);
        block = next;
    }
    a->head = NULL;
}

void buf_init(buf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

int buf_reserve(buf *b, size_t extra) {
    if (b->cap - b->len >= extra && b->data != NULL) {
        return 1;
    }
    size_t want = b->len + extra + 1;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < want) {
        cap *= 2;
    }
    char *grown = (char *)realloc(b->data, cap);
    if (grown == NULL) {
        return 0;
    }
    b->data = grown;
    b->cap = cap;
    return 1;
}

int buf_put(buf *b, const void *data, size_t len) {
    if (len == 0) {
        return 1;
    }
    if (!buf_reserve(b, len)) {
        return 0;
    }
    memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = 0;
    return 1;
}

int buf_puts(buf *b, const char *text) {
    return buf_put(b, text, strlen(text));
}

int buf_putc(buf *b, char ch) {
    return buf_put(b, &ch, 1);
}

int buf_printf(buf *b, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int need = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (need < 0) {
        return 0;
    }
    if (!buf_reserve(b, (size_t)need + 1)) {
        return 0;
    }
    va_start(args, fmt);
    vsnprintf(b->data + b->len, (size_t)need + 1, fmt, args);
    va_end(args);
    b->len += (size_t)need;
    return 1;
}

void buf_reset(buf *b) {
    b->len = 0;
    if (b->data != NULL) {
        b->data[0] = 0;
    }
}

void buf_free(buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void err_clear(err *e) {
    e->text[0] = 0;
    e->set = 0;
}

void err_set(err *e, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(e->text, sizeof(e->text), fmt, args);
    va_end(args);
    e->set = 1;
}

wchar_t *utf8_to_wide(const char *text) {
    int need = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (need <= 0) {
        return NULL;
    }
    wchar_t *out = (wchar_t *)malloc((size_t)need * sizeof(wchar_t));
    if (out == NULL) {
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, out, need) <= 0) {
        free(out);
        return NULL;
    }
    return out;
}

char *wide_to_utf8(const wchar_t *text) {
    int need = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (need <= 0) {
        return NULL;
    }
    char *out = (char *)malloc((size_t)need);
    if (out == NULL) {
        return NULL;
    }
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, out, need, NULL, NULL) <= 0) {
        free(out);
        return NULL;
    }
    return out;
}

wchar_t *path_to_wide(const char *path) {
    wchar_t *raw = utf8_to_wide(path);
    if (raw == NULL) {
        return NULL;
    }
    for (wchar_t *cursor = raw; *cursor; cursor++) {
        if (*cursor == L'/') {
            *cursor = L'\\';
        }
    }
    if (wcsncmp(raw, L"\\\\?\\", 4) == 0) {
        return raw;
    }
    DWORD need = GetFullPathNameW(raw, 0, NULL, NULL);
    if (need == 0) {
        return raw;
    }
    wchar_t *full = (wchar_t *)malloc((size_t)need * sizeof(wchar_t));
    if (full == NULL) {
        free(raw);
        return NULL;
    }
    DWORD got = GetFullPathNameW(raw, need, full, NULL);
    free(raw);
    if (got == 0 || got >= need) {
        free(full);
        return NULL;
    }
    int unc = full[0] == L'\\' && full[1] == L'\\';
    size_t extra = unc ? 7 : 4;
    wchar_t *out = (wchar_t *)malloc(((size_t)got + extra + 1) * sizeof(wchar_t));
    if (out == NULL) {
        free(full);
        return NULL;
    }
    if (unc) {
        memcpy(out, L"\\\\?\\UNC", 7 * sizeof(wchar_t));
        memcpy(out + 7, full + 1, ((size_t)got) * sizeof(wchar_t));
    } else {
        memcpy(out, L"\\\\?\\", 4 * sizeof(wchar_t));
        memcpy(out + 4, full, ((size_t)got + 1) * sizeof(wchar_t));
    }
    free(full);
    return out;
}

static size_t wide_root_len(const wchar_t *path) {
    size_t at = 0;
    if (wcsncmp(path, L"\\\\?\\", 4) == 0) {
        at = 4;
        if (wcsncmp(path + at, L"UNC\\", 4) == 0) {
            at += 4;
            for (int part = 0; part < 2 && path[at]; part++) {
                while (path[at] && path[at] != L'\\') {
                    at++;
                }
                if (path[at] == L'\\') {
                    at++;
                }
            }
            return at;
        }
    }
    if (path[at] != 0 && path[at + 1] == L':') {
        at += 2;
        if (path[at] == L'\\') {
            at++;
        }
    }
    return at;
}

void path_to_slash(char *text) {
    for (char *cursor = text; *cursor; cursor++) {
        if (*cursor == '\\') {
            *cursor = '/';
        }
    }
}

void path_to_backslash(char *text) {
    for (char *cursor = text; *cursor; cursor++) {
        if (*cursor == '/') {
            *cursor = '\\';
        }
    }
}

char *path_join(const char *base, const char *tail) {
    size_t base_len = strlen(base);
    size_t tail_len = strlen(tail);
    while (base_len > 0 && (base[base_len - 1] == '/' || base[base_len - 1] == '\\')) {
        base_len--;
    }
    while (tail_len > 0 && (*tail == '/' || *tail == '\\')) {
        tail++;
        tail_len--;
    }
    char *out = (char *)malloc(base_len + tail_len + 2);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, base, base_len);
    size_t at = base_len;
    if (base_len > 0 && tail_len > 0) {
        out[at++] = '\\';
    }
    memcpy(out + at, tail, tail_len);
    at += tail_len;
    out[at] = 0;
    return out;
}

static DWORD path_attributes(const char *path) {
    wchar_t *wide = path_to_wide(path);
    if (wide == NULL) {
        return INVALID_FILE_ATTRIBUTES;
    }
    DWORD attrs = GetFileAttributesW(wide);
    free(wide);
    return attrs;
}

int path_is_dir(const char *path) {
    DWORD attrs = path_attributes(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

int path_is_file(const char *path) {
    DWORD attrs = path_attributes(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

int path_make_dirs(const char *path) {
    wchar_t *wide = path_to_wide(path);
    if (wide == NULL) {
        return 0;
    }
    size_t len = wcslen(wide);
    size_t start = wide_root_len(wide);
    if (start == 0) {
        start = 1;
    }
    for (size_t i = start; i <= len; i++) {
        if (i == len || wide[i] == L'\\') {
            wchar_t saved = wide[i];
            wide[i] = 0;
            DWORD attrs = GetFileAttributesW(wide);
            if (attrs == INVALID_FILE_ATTRIBUTES) {
                if (!CreateDirectoryW(wide, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
                    wide[i] = saved;
                    free(wide);
                    return 0;
                }
            } else if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                wide[i] = saved;
                free(wide);
                return 0;
            }
            wide[i] = saved;
        }
    }
    free(wide);
    return 1;
}

int path_make_parent_dirs(const char *path) {
    size_t len = strlen(path);
    while (len > 0 && path[len - 1] != '/' && path[len - 1] != '\\') {
        len--;
    }
    if (len == 0) {
        return 1;
    }
    char *parent = (char *)malloc(len + 1);
    if (parent == NULL) {
        return 0;
    }
    memcpy(parent, path, len);
    parent[len] = 0;
    int ok = path_make_dirs(parent);
    free(parent);
    return ok;
}

int64_t path_file_size(const char *path) {
    wchar_t *wide = path_to_wide(path);
    if (wide == NULL) {
        return -1;
    }
    WIN32_FILE_ATTRIBUTE_DATA info;
    BOOL ok = GetFileAttributesExW(wide, GetFileExInfoStandard, &info);
    free(wide);
    if (!ok) {
        return -1;
    }
    return ((int64_t)info.nFileSizeHigh << 32) | (int64_t)info.nFileSizeLow;
}

int path_delete(const char *path) {
    wchar_t *wide = path_to_wide(path);
    if (wide == NULL) {
        return 0;
    }
    BOOL ok = DeleteFileW(wide);
    free(wide);
    return ok ? 1 : 0;
}

int path_rename(const char *from, const char *to) {
    wchar_t *wide_from = path_to_wide(from);
    wchar_t *wide_to = path_to_wide(to);
    int ok = 0;
    if (wide_from != NULL && wide_to != NULL) {
        ok = MoveFileExW(wide_from, wide_to, MOVEFILE_REPLACE_EXISTING) ? 1 : 0;
    }
    free(wide_from);
    free(wide_to);
    return ok;
}

static const char *const reserved_names[] = {
    "CON", "PRN", "AUX", "NUL",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
};

static int is_reserved_stem(const char *part, size_t len) {
    size_t stem = 0;
    while (stem < len && part[stem] != '.') {
        stem++;
    }
    if (stem == 0 || stem > 4) {
        return 0;
    }
    char upper[5];
    for (size_t i = 0; i < stem; i++) {
        char ch = part[i];
        upper[i] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch;
    }
    upper[stem] = 0;
    for (size_t i = 0; i < sizeof(reserved_names) / sizeof(reserved_names[0]); i++) {
        if (strcmp(upper, reserved_names[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

char *path_sanitize_relative(const char *relative) {
    size_t len = strlen(relative);
    char *out = (char *)malloc(len * 2 + 16);
    if (out == NULL) {
        return NULL;
    }
    size_t at = 0;
    size_t i = 0;
    int wrote_any = 0;
    while (i <= len) {
        size_t start = i;
        while (i < len && relative[i] != '/' && relative[i] != '\\') {
            i++;
        }
        size_t part_len = i - start;
        const char *part = relative + start;
        int skip = part_len == 0;
        if (part_len == 1 && part[0] == '.') {
            skip = 1;
        }
        if (part_len == 2 && part[0] == '.' && part[1] == '.') {
            skip = 1;
        }
        if (!skip) {
            if (wrote_any) {
                out[at++] = '\\';
            }
            if (is_reserved_stem(part, part_len)) {
                out[at++] = '_';
            }
            size_t written = 0;
            for (size_t k = 0; k < part_len; k++) {
                unsigned char ch = (unsigned char)part[k];
                if (ch < 32 || ch == '<' || ch == '>' || ch == ':' || ch == '"' ||
                    ch == '|' || ch == '?' || ch == '*') {
                    out[at++] = '_';
                } else {
                    out[at++] = (char)ch;
                }
                written++;
            }
            while (written > 0 && (out[at - 1] == ' ' || out[at - 1] == '.')) {
                at--;
                written--;
            }
            if (written == 0) {
                out[at++] = '_';
            }
            wrote_any = 1;
        }
        i++;
    }
    if (!wrote_any) {
        memcpy(out, "unnamed", 7);
        at = 7;
    }
    out[at] = 0;
    return out;
}

FILE *file_open(const char *path, const char *mode) {
    wchar_t *wide_path = path_to_wide(path);
    if (wide_path == NULL) {
        return NULL;
    }
    wchar_t wide_mode[8];
    size_t mode_len = strlen(mode);
    if (mode_len >= sizeof(wide_mode) / sizeof(wide_mode[0])) {
        free(wide_path);
        return NULL;
    }
    for (size_t i = 0; i <= mode_len; i++) {
        wide_mode[i] = (wchar_t)mode[i];
    }
    FILE *handle = _wfopen(wide_path, wide_mode);
    free(wide_path);
    return handle;
}

int file_truncate(const char *path, int64_t size) {
    wchar_t *wide = path_to_wide(path);
    if (wide == NULL) {
        return 0;
    }
    HANDLE handle = CreateFileW(wide, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wide);
    if (handle == INVALID_HANDLE_VALUE) {
        return 0;
    }
    LARGE_INTEGER where;
    where.QuadPart = size;
    int ok = SetFilePointerEx(handle, where, NULL, FILE_BEGIN) && SetEndOfFile(handle);
    CloseHandle(handle);
    return ok ? 1 : 0;
}

int file_read_all(const char *path, buf *out) {
    int64_t size = path_file_size(path);
    if (size < 0) {
        return 0;
    }
    FILE *handle = file_open(path, "rb");
    if (handle == NULL) {
        return 0;
    }
    buf_reset(out);
    if (!buf_reserve(out, (size_t)size)) {
        fclose(handle);
        return 0;
    }
    size_t got = size > 0 ? fread(out->data, 1, (size_t)size, handle) : 0;
    fclose(handle);
    if (got != (size_t)size) {
        return 0;
    }
    out->len = got;
    out->data[got] = 0;
    return 1;
}

int file_write_prefixed(const char *path, const void *prefix, size_t prefix_len,
                        const void *data, size_t len) {
    wchar_t *wide = path_to_wide(path);
    if (wide == NULL) {
        return 0;
    }
    HANDLE handle = CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    free(wide);
    if (handle == INVALID_HANDLE_VALUE) {
        return 0;
    }

    int ok = 1;
    const void *parts[2] = {prefix, data};
    size_t sizes[2] = {prefix_len, len};
    for (int p = 0; p < 2 && ok; p++) {
        size_t off = 0;
        while (off < sizes[p]) {
            size_t remaining = sizes[p] - off;
            DWORD chunk = remaining > 0x40000000u ? 0x40000000u : (DWORD)remaining;
            DWORD wrote = 0;
            if (!WriteFile(handle, (const unsigned char *)parts[p] + off, chunk,
                           &wrote, NULL) || wrote != chunk) {
                ok = 0;
                break;
            }
            off += wrote;
        }
    }

    CloseHandle(handle);
    return ok;
}

int file_write_prepared(const char *path, const void *data, size_t len) {
    wchar_t *wide = path_to_wide(path);
    if (wide == NULL) {
        return 0;
    }
    HANDLE handle = CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    free(wide);
    if (handle == INVALID_HANDLE_VALUE) {
        return 0;
    }

    int ok = 1;
    size_t off = 0;
    while (off < len) {
        size_t remaining = len - off;
        DWORD chunk = remaining > 0x40000000u ? 0x40000000u : (DWORD)remaining;
        DWORD wrote = 0;
        if (!WriteFile(handle, (const unsigned char *)data + off, chunk, &wrote, NULL) ||
            wrote != chunk) {
            ok = 0;
            break;
        }
        off += wrote;
    }

    CloseHandle(handle);
    return ok;
}

int file_write_all(const char *path, const void *data, size_t len) {
    if (!path_make_parent_dirs(path)) {
        return 0;
    }
    return file_write_prepared(path, data, len);
}

int file_write_atomic(const char *path, const void *data, size_t len) {
    size_t path_len = strlen(path);
    char *temp = (char *)malloc(path_len + 5);
    if (temp == NULL) {
        return 0;
    }
    memcpy(temp, path, path_len);
    memcpy(temp + path_len, ".tmp", 5);
    int ok = file_write_all(temp, data, len);
    if (ok) {
        ok = path_rename(temp, path);
    }
    if (!ok) {
        path_delete(temp);
    }
    free(temp);
    return ok;
}

typedef struct {
    uint32_t state[8];
    uint64_t bits;
    unsigned char block[64];
    size_t fill;
} sha256_ctx;

static const uint32_t sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static uint32_t rotr32(uint32_t value, int count) {
    return (value >> count) | (value << (32 - count));
}

static void sha256_compress(sha256_ctx *ctx, const unsigned char *block) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + sha256_k[i] + w[i];
        uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_start(sha256_ctx *ctx) {
    ctx->state[0] = 0x6a09e667u; ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u; ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu; ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu; ctx->state[7] = 0x5be0cd19u;
    ctx->bits = 0;
    ctx->fill = 0;
}

static void sha256_update(sha256_ctx *ctx, const unsigned char *data, size_t len) {
    ctx->bits += (uint64_t)len * 8u;
    while (len > 0) {
        size_t room = 64 - ctx->fill;
        size_t take = len < room ? len : room;
        memcpy(ctx->block + ctx->fill, data, take);
        ctx->fill += take;
        data += take;
        len -= take;
        if (ctx->fill == 64) {
            sha256_compress(ctx, ctx->block);
            ctx->fill = 0;
        }
    }
}

static void sha256_finish(sha256_ctx *ctx, char out[65]) {
    uint64_t bits = ctx->bits;
    unsigned char pad = 0x80;
    sha256_update(ctx, &pad, 1);
    unsigned char zero = 0;
    while (ctx->fill != 56) {
        sha256_update(ctx, &zero, 1);
    }
    unsigned char tail[8];
    for (int i = 0; i < 8; i++) {
        tail[i] = (unsigned char)(bits >> (56 - i * 8));
    }
    ctx->bits = bits;
    memcpy(ctx->block + 56, tail, 8);
    sha256_compress(ctx, ctx->block);
    ctx->fill = 0;
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        for (int k = 0; k < 4; k++) {
            unsigned char byte = (unsigned char)(ctx->state[i] >> (24 - k * 8));
            out[i * 8 + k * 2] = digits[byte >> 4];
            out[i * 8 + k * 2 + 1] = digits[byte & 15];
        }
    }
    out[64] = 0;
}

void sha256_bytes(const void *data, size_t len, char out[65]) {
    sha256_ctx ctx;
    sha256_start(&ctx);
    sha256_update(&ctx, (const unsigned char *)data, len);
    sha256_finish(&ctx, out);
}

int sha256_file(const char *path, char out[65]) {
    FILE *handle = file_open(path, "rb");
    if (handle == NULL) {
        return 0;
    }
    sha256_ctx ctx;
    sha256_start(&ctx);
    unsigned char *chunk = (unsigned char *)malloc(1u << 20);
    if (chunk == NULL) {
        fclose(handle);
        return 0;
    }
    for (;;) {
        size_t got = fread(chunk, 1, 1u << 20, handle);
        if (got == 0) {
            break;
        }
        sha256_update(&ctx, chunk, got);
    }
    int ok = ferror(handle) == 0;
    free(chunk);
    fclose(handle);
    if (ok) {
        sha256_finish(&ctx, out);
    }
    return ok;
}

const char *utc_now_iso(char out[32]) {
    time_t now = time(NULL);
    struct tm parts;
    gmtime_s(&parts, &now);
    strftime(out, 32, "%Y-%m-%dT%H:%M:%S+00:00", &parts);
    return out;
}

unsigned cpu_count(void) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    unsigned count = (unsigned)info.dwNumberOfProcessors;
    return count == 0 ? 1u : count;
}
