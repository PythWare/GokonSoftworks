#ifndef UTIL_H
#define UTIL_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define ARENA_BLOCK_MIN (1u << 20)
#define ERR_MAX 512

typedef struct arena_block {
    struct arena_block *next;
    size_t used;
    size_t cap;
} arena_block;

typedef struct {
    arena_block *head;
} arena;

void arena_init(arena *a);
void arena_reset(arena *a);
void *arena_alloc(arena *a, size_t size);
char *arena_strdup(arena *a, const char *text, size_t len);
void arena_free(arena *a);

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} buf;

void buf_init(buf *b);
int buf_reserve(buf *b, size_t extra);
int buf_put(buf *b, const void *data, size_t len);
int buf_puts(buf *b, const char *text);
int buf_putc(buf *b, char ch);
int buf_printf(buf *b, const char *fmt, ...);
void buf_reset(buf *b);
void buf_free(buf *b);

typedef struct {
    char text[ERR_MAX];
    int set;
} err;

void err_clear(err *e);
void err_set(err *e, const char *fmt, ...);

wchar_t *utf8_to_wide(const char *text);
char *wide_to_utf8(const wchar_t *text);
wchar_t *path_to_wide(const char *path);

void path_to_slash(char *text);
void path_to_backslash(char *text);
char *path_join(const char *base, const char *tail);
int path_is_dir(const char *path);
int path_is_file(const char *path);
int path_make_dirs(const char *path);
int path_make_parent_dirs(const char *path);
int64_t path_file_size(const char *path);
int path_delete(const char *path);
int path_rename(const char *from, const char *to);

char *path_sanitize_relative(const char *relative);

FILE *file_open(const char *path, const char *mode);
int file_truncate(const char *path, int64_t size);
int file_read_all(const char *path, buf *out);
int file_write_all(const char *path, const void *data, size_t len);
int file_write_prepared(const char *path, const void *data, size_t len);
int file_write_prefixed(const char *path, const void *prefix, size_t prefix_len,
                        const void *data, size_t len);
int file_write_atomic(const char *path, const void *data, size_t len);

void sha256_bytes(const void *data, size_t len, char out[65]);
int sha256_file(const char *path, char out[65]);

const char *utc_now_iso(char out[32]);
unsigned cpu_count(void);

#endif
