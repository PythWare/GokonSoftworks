#ifndef GSJSON_H
#define GSJSON_H
#include "json.h"
#include "util.h"

json_value *jv_num(arena *a, double number);
json_value *jv_str(arena *a, const char *text);
json_value *jv_strn(arena *a, const char *text, size_t len);
json_value *jv_bool(arena *a, int value);
json_value *jv_null(arena *a);
json_value *jv_arr_from(arena *a, json_value **items, size_t count);
json_value *jv_obj_from(arena *a, char **keys, json_value **values, size_t count);
json_value *jv_copy(arena *a, const json_value *node);

int gsjson_dump(const json_value *node, int indent, buf *out);
int gsjson_dump_text(const json_value *node, int indent, buf *out);
int gsjson_write_file(const char *path, const json_value *node);
int gsjson_write_text(const char *path, const char *text, size_t len);
json_value *gsjson_read_file(arena *a, const char *path);
json_value *gsjson_read_or_empty(arena *a, const char *path);

#endif
