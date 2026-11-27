#include "gsjson.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static json_value *jv_new(arena *a, json_kind kind) {
    json_value *node = (json_value *)arena_alloc(a, sizeof(json_value));
    if (node != NULL) {
        memset(node, 0, sizeof(*node));
        node->kind = kind;
    }
    return node;
}

json_value *jv_num(arena *a, double number) {
    json_value *node = jv_new(a, JSON_NUM);
    if (node != NULL) {
        node->number = number;
    }
    return node;
}

json_value *jv_strn(arena *a, const char *text, size_t len) {
    json_value *node = jv_new(a, JSON_STR);
    if (node == NULL) {
        return NULL;
    }
    node->text = arena_strdup(a, text == NULL ? "" : text, text == NULL ? 0 : len);
    node->text_len = text == NULL ? 0 : len;
    return node->text == NULL ? NULL : node;
}

json_value *jv_str(arena *a, const char *text) {
    return jv_strn(a, text, text == NULL ? 0 : strlen(text));
}

json_value *jv_bool(arena *a, int value) {
    json_value *node = jv_new(a, JSON_BOOL);
    if (node != NULL) {
        node->boolean = value ? 1 : 0;
    }
    return node;
}

json_value *jv_null(arena *a) {
    return jv_new(a, JSON_NULL);
}

json_value *jv_arr_from(arena *a, json_value **items, size_t count) {
    json_value *node = jv_new(a, JSON_ARR);
    if (node == NULL) {
        return NULL;
    }
    node->items = (json_value **)arena_alloc(a, (count + 1) * sizeof(json_value *));
    if (node->items == NULL) {
        return NULL;
    }
    if (count > 0) {
        memcpy(node->items, items, count * sizeof(json_value *));
    }
    node->count = count;
    return node;
}

json_value *jv_obj_from(arena *a, char **keys, json_value **values, size_t count) {
    json_value *node = jv_new(a, JSON_OBJ);
    if (node == NULL) {
        return NULL;
    }
    node->items = (json_value **)arena_alloc(a, (count + 1) * sizeof(json_value *));
    node->keys = (char **)arena_alloc(a, (count + 1) * sizeof(char *));
    if (node->items == NULL || node->keys == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        node->keys[i] = arena_strdup(a, keys[i], strlen(keys[i]));
        if (node->keys[i] == NULL) {
            return NULL;
        }
        node->items[i] = values[i];
    }
    node->count = count;
    return node;
}

json_value *jv_copy(arena *a, const json_value *node) {
    if (node == NULL) {
        return NULL;
    }
    json_value *out = jv_new(a, node->kind);
    if (out == NULL) {
        return NULL;
    }
    out->boolean = node->boolean;
    out->number = node->number;
    if (node->kind == JSON_STR) {
        out->text = arena_strdup(a, node->text == NULL ? "" : node->text, node->text_len);
        out->text_len = node->text_len;
        return out->text == NULL ? NULL : out;
    }
    if (node->kind == JSON_ARR || node->kind == JSON_OBJ) {
        out->items = (json_value **)arena_alloc(a, (node->count + 1) * sizeof(json_value *));
        if (out->items == NULL) {
            return NULL;
        }
        if (node->kind == JSON_OBJ) {
            out->keys = (char **)arena_alloc(a, (node->count + 1) * sizeof(char *));
            if (out->keys == NULL) {
                return NULL;
            }
        }
        for (size_t i = 0; i < node->count; i++) {
            out->items[i] = jv_copy(a, node->items[i]);
            if (out->items[i] == NULL) {
                return NULL;
            }
            if (node->kind == JSON_OBJ) {
                const char *key = node->keys[i] == NULL ? "" : node->keys[i];
                out->keys[i] = arena_strdup(a, key, strlen(key));
                if (out->keys[i] == NULL) {
                    return NULL;
                }
            }
        }
        out->count = node->count;
    }
    return out;
}

static int put_hex4(buf *out, unsigned code) {
    static const char digits[] = "0123456789abcdef";
    char esc[6] = {'\\', 'u', digits[(code >> 12) & 15], digits[(code >> 8) & 15],
                   digits[(code >> 4) & 15], digits[code & 15]};
    return buf_put(out, esc, 6);
}

static size_t utf8_decode(const unsigned char *text, size_t len, unsigned *code) {
    unsigned ch = text[0];
    size_t need = 0;
    unsigned value = 0;
    if (ch < 0x80u) {
        *code = ch;
        return 1;
    }
    if ((ch & 0xE0u) == 0xC0u) {
        need = 2;
        value = ch & 0x1Fu;
    } else if ((ch & 0xF0u) == 0xE0u) {
        need = 3;
        value = ch & 0x0Fu;
    } else if ((ch & 0xF8u) == 0xF0u) {
        need = 4;
        value = ch & 0x07u;
    }
    if (need == 0 || need > len) {
        *code = 0xFFFDu;
        return 1;
    }
    for (size_t k = 1; k < need; k++) {
        if ((text[k] & 0xC0u) != 0x80u) {
            *code = 0xFFFDu;
            return 1;
        }
        value = (value << 6) | (text[k] & 0x3Fu);
    }
    *code = value;
    return need;
}

static int dump_string(const char *text, size_t len, buf *out) {
    int ok = buf_putc(out, '"');
    const unsigned char *raw = (const unsigned char *)text;
    size_t i = 0;
    while (ok && i < len) {
        unsigned code = 0;
        size_t used = utf8_decode(raw + i, len - i, &code);
        i += used;
        switch (code) {
            case '"': ok = buf_put(out, "\\\"", 2); continue;
            case '\\': ok = buf_put(out, "\\\\", 2); continue;
            case '\n': ok = buf_put(out, "\\n", 2); continue;
            case '\r': ok = buf_put(out, "\\r", 2); continue;
            case '\t': ok = buf_put(out, "\\t", 2); continue;
            case '\b': ok = buf_put(out, "\\b", 2); continue;
            case '\f': ok = buf_put(out, "\\f", 2); continue;
            default: break;
        }
        if (code >= 0x20u && code <= 0x7Eu) {
            ok = buf_putc(out, (char)code);
        } else if (code > 0xFFFFu) {
            unsigned rest = code - 0x10000u;
            ok = put_hex4(out, 0xD800u | (rest >> 10)) && put_hex4(out, 0xDC00u | (rest & 0x3FFu));
        } else {
            ok = put_hex4(out, code);
        }
    }
    return ok && buf_putc(out, '"');
}

static int dump_number(double number, buf *out) {
    if (number == trunc(number) && fabs(number) < 9.2e18) {
        return buf_printf(out, "%lld", (long long)number);
    }
    char text[40];
    for (int precision = 1; precision <= 17; precision++) {
        snprintf(text, sizeof(text), "%.*g", precision, number);
        if (strtod(text, NULL) == number) {
            break;
        }
    }
    return buf_puts(out, text);
}

static int dump_indent(buf *out, int indent, int level) {
    if (!buf_putc(out, '\n')) {
        return 0;
    }
    for (int i = 0; i < indent * level; i++) {
        if (!buf_putc(out, ' ')) {
            return 0;
        }
    }
    return 1;
}

static int dump_node(const json_value *node, int indent, int level, buf *out) {
    if (node == NULL) {
        return buf_puts(out, "null");
    }
    switch (node->kind) {
        case JSON_NULL: return buf_puts(out, "null");
        case JSON_BOOL: return buf_puts(out, node->boolean ? "true" : "false");
        case JSON_NUM: return dump_number(node->number, out);
        case JSON_STR: return dump_string(node->text == NULL ? "" : node->text, node->text_len, out);
        default: break;
    }
    int is_obj = node->kind == JSON_OBJ;
    if (node->count == 0) {
        return buf_puts(out, is_obj ? "{}" : "[]");
    }
    if (!buf_putc(out, is_obj ? '{' : '[')) {
        return 0;
    }
    for (size_t i = 0; i < node->count; i++) {
        if (i > 0 && !buf_putc(out, ',')) {
            return 0;
        }
        if (indent > 0) {
            if (!dump_indent(out, indent, level + 1)) {
                return 0;
            }
        } else if (i > 0 && !buf_putc(out, ' ')) {
            return 0;
        }
        if (is_obj) {
            const char *key = node->keys[i] == NULL ? "" : node->keys[i];
            if (!dump_string(key, strlen(key), out) || !buf_put(out, ": ", 2)) {
                return 0;
            }
        }
        if (!dump_node(node->items[i], indent, level + 1, out)) {
            return 0;
        }
    }
    if (indent > 0 && !dump_indent(out, indent, level)) {
        return 0;
    }
    return buf_putc(out, is_obj ? '}' : ']');
}

int gsjson_dump(const json_value *node, int indent, buf *out) {
    return dump_node(node, indent, 0, out);
}

int gsjson_dump_text(const json_value *node, int indent, buf *out) {
    buf plain;
    buf_init(&plain);
    int ok = dump_node(node, indent, 0, &plain);
    for (size_t i = 0; ok && i < plain.len; i++) {
        if (plain.data[i] == '\n') {
            ok = buf_putc(out, '\r');
        }
        if (ok) {
            ok = buf_putc(out, plain.data[i]);
        }
    }
    buf_free(&plain);
    return ok;
}

int gsjson_write_text(const char *path, const char *text, size_t len) {
    if (!path_make_parent_dirs(path)) {
        return 0;
    }
    return file_write_atomic(path, text, len);
}

int gsjson_write_file(const char *path, const json_value *node) {
    buf text;
    buf_init(&text);
    int ok = gsjson_dump_text(node, 1, &text) &&
             gsjson_write_text(path, text.data == NULL ? "" : text.data, text.len);
    buf_free(&text);
    return ok;
}

json_value *gsjson_read_file(arena *a, const char *path) {
    buf text;
    buf_init(&text);
    if (!file_read_all(path, &text)) {
        buf_free(&text);
        return NULL;
    }
    char *copy = arena_strdup(a, text.data == NULL ? "" : text.data, text.len);
    size_t len = text.len;
    buf_free(&text);
    if (copy == NULL) {
        return NULL;
    }
    err e;
    err_clear(&e);
    return json_parse(a, copy, len, &e);
}

json_value *gsjson_read_or_empty(arena *a, const char *path) {
    json_value *node = gsjson_read_file(a, path);
    if (node == NULL || node->kind != JSON_OBJ) {
        return jv_obj_from(a, NULL, NULL, 0);
    }
    return node;
}
