#ifndef JSON_H
#define JSON_H
#include "util.h"
#define JSON_DEPTH_MAX 64
#define JSON_FLUSH_AT (1u << 20)

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUM,
    JSON_STR,
    JSON_ARR,
    JSON_OBJ
} json_kind;

typedef struct json_value json_value;

struct json_value {
    json_kind kind;
    int boolean;
    double number;
    char *text;
    size_t text_len;
    json_value **items;
    char **keys;
    size_t count;
};

json_value *json_parse(arena *a, const char *text, size_t len, err *e);
json_value *json_obj_get(const json_value *node, const char *key);
const char *json_as_str(const json_value *node, const char *fallback);
int64_t json_as_i64(const json_value *node, int64_t fallback);
int json_as_bool(const json_value *node, int fallback);
const json_value *json_at(const json_value *node, size_t index);
size_t json_count(const json_value *node);

typedef struct {
    buf out;
    FILE *sink;
    int has_item[JSON_DEPTH_MAX];
    int depth;
    int failed;
} json_writer;

void jw_init(json_writer *w, FILE *sink);
void jw_free(json_writer *w);
void jw_obj_open(json_writer *w);
void jw_obj_close(json_writer *w);
void jw_arr_open(json_writer *w);
void jw_arr_close(json_writer *w);
void jw_key(json_writer *w, const char *key);
void jw_key_n(json_writer *w, const char *key, size_t len);
void jw_str(json_writer *w, const char *text);
void jw_strn(json_writer *w, const char *text, size_t len);
void jw_i64(json_writer *w, int64_t value);
void jw_u64(json_writer *w, uint64_t value);
void jw_bool(json_writer *w, int value);
void jw_null(json_writer *w);
void jw_kv_str(json_writer *w, const char *key, const char *text);
void jw_kv_strn(json_writer *w, const char *key, const char *text, size_t len);
void jw_kv_i64(json_writer *w, const char *key, int64_t value);
void jw_kv_u64(json_writer *w, const char *key, uint64_t value);
void jw_kv_bool(json_writer *w, const char *key, int value);
int jw_finish(json_writer *w);

#endif
