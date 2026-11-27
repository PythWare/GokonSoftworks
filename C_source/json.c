#include "json.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *text;
    size_t len;
    size_t at;
    arena *a;
    err *e;
} json_reader;

static json_value *parse_value(json_reader *r);

static void skip_space(json_reader *r) {
    while (r->at < r->len) {
        char ch = r->text[r->at];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            r->at++;
        } else {
            break;
        }
    }
}

static json_value *new_value(json_reader *r, json_kind kind) {
    json_value *node = (json_value *)arena_alloc(r->a, sizeof(json_value));
    if (node == NULL) {
        err_set(r->e, "Out of memory parsing JSON");
        return NULL;
    }
    memset(node, 0, sizeof(*node));
    node->kind = kind;
    return node;
}

static int read_hex4(json_reader *r, unsigned *out) {
    if (r->at + 4 > r->len) {
        return 0;
    }
    unsigned value = 0;
    for (int i = 0; i < 4; i++) {
        char ch = r->text[r->at + i];
        value <<= 4;
        if (ch >= '0' && ch <= '9') {
            value |= (unsigned)(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            value |= (unsigned)(ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            value |= (unsigned)(ch - 'A' + 10);
        } else {
            return 0;
        }
    }
    r->at += 4;
    *out = value;
    return 1;
}

static size_t encode_utf8(unsigned code, char *out) {
    if (code < 0x80u) {
        out[0] = (char)code;
        return 1;
    }
    if (code < 0x800u) {
        out[0] = (char)(0xC0u | (code >> 6));
        out[1] = (char)(0x80u | (code & 0x3Fu));
        return 2;
    }
    if (code < 0x10000u) {
        out[0] = (char)(0xE0u | (code >> 12));
        out[1] = (char)(0x80u | ((code >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (code & 0x3Fu));
        return 3;
    }
    out[0] = (char)(0xF0u | (code >> 18));
    out[1] = (char)(0x80u | ((code >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((code >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (code & 0x3Fu));
    return 4;
}

static json_value *parse_string(json_reader *r) {
    r->at++;
    size_t start = r->at;
    size_t escapes = 0;
    while (r->at < r->len && r->text[r->at] != '"') {
        if (r->text[r->at] == '\\') {
            escapes++;
            r->at++;
            if (r->at >= r->len) {
                break;
            }
        }
        r->at++;
    }
    if (r->at >= r->len) {
        err_set(r->e, "Unterminated string in JSON");
        return NULL;
    }
    size_t raw_len = r->at - start;
    r->at++;

    json_value *node = new_value(r, JSON_STR);
    if (node == NULL) {
        return NULL;
    }
    char *out = (char *)arena_alloc(r->a, raw_len + 1);
    if (out == NULL) {
        err_set(r->e, "Out of memory parsing JSON string");
        return NULL;
    }
    if (escapes == 0) {
        memcpy(out, r->text + start, raw_len);
        out[raw_len] = 0;
        node->text = out;
        node->text_len = raw_len;
        return node;
    }

    size_t at = 0;
    for (size_t i = start; i < start + raw_len;) {
        char ch = r->text[i];
        if (ch != '\\') {
            out[at++] = ch;
            i++;
            continue;
        }
        i++;
        if (i >= start + raw_len) {
            break;
        }
        char code = r->text[i++];
        switch (code) {
            case '"': out[at++] = '"'; break;
            case '\\': out[at++] = '\\'; break;
            case '/': out[at++] = '/'; break;
            case 'b': out[at++] = '\b'; break;
            case 'f': out[at++] = '\f'; break;
            case 'n': out[at++] = '\n'; break;
            case 'r': out[at++] = '\r'; break;
            case 't': out[at++] = '\t'; break;
            case 'u': {
                size_t saved = r->at;
                r->at = i;
                unsigned code_point = 0;
                if (!read_hex4(r, &code_point)) {
                    r->at = saved;
                    err_set(r->e, "Bad \\u escape in JSON");
                    return NULL;
                }
                i = r->at;
                if (code_point >= 0xD800u && code_point <= 0xDBFFu &&
                    i + 1 < start + raw_len && r->text[i] == '\\' && r->text[i + 1] == 'u') {
                    r->at = i + 2;
                    unsigned low = 0;
                    if (read_hex4(r, &low) && low >= 0xDC00u && low <= 0xDFFFu) {
                        code_point = 0x10000u + ((code_point - 0xD800u) << 10) + (low - 0xDC00u);
                        i = r->at;
                    }
                }
                r->at = saved;
                at += encode_utf8(code_point, out + at);
                break;
            }
            default:
                out[at++] = code;
                break;
        }
    }
    out[at] = 0;
    node->text = out;
    node->text_len = at;
    return node;
}

static json_value *parse_number(json_reader *r) {
    char scratch[64];
    size_t at = 0;
    while (r->at < r->len && at + 1 < sizeof(scratch)) {
        char ch = r->text[r->at];
        if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' ||
            ch == 'e' || ch == 'E') {
            scratch[at++] = ch;
            r->at++;
        } else {
            break;
        }
    }
    scratch[at] = 0;
    json_value *node = new_value(r, JSON_NUM);
    if (node == NULL) {
        return NULL;
    }
    node->number = strtod(scratch, NULL);
    return node;
}

static json_value *parse_array(json_reader *r) {
    r->at++;
    json_value *node = new_value(r, JSON_ARR);
    if (node == NULL) {
        return NULL;
    }
    size_t cap = 8;
    json_value **items = (json_value **)arena_alloc(r->a, cap * sizeof(json_value *));
    if (items == NULL) {
        err_set(r->e, "Out of memory parsing JSON array");
        return NULL;
    }
    skip_space(r);
    if (r->at < r->len && r->text[r->at] == ']') {
        r->at++;
        node->items = items;
        return node;
    }
    for (;;) {
        skip_space(r);
        json_value *item = parse_value(r);
        if (item == NULL) {
            return NULL;
        }
        if (node->count == cap) {
            size_t grown = cap * 2;
            json_value **bigger = (json_value **)arena_alloc(r->a, grown * sizeof(json_value *));
            if (bigger == NULL) {
                err_set(r->e, "Out of memory growing JSON array");
                return NULL;
            }
            memcpy(bigger, items, cap * sizeof(json_value *));
            items = bigger;
            cap = grown;
        }
        items[node->count++] = item;
        skip_space(r);
        if (r->at < r->len && r->text[r->at] == ',') {
            r->at++;
            continue;
        }
        if (r->at < r->len && r->text[r->at] == ']') {
            r->at++;
            break;
        }
        err_set(r->e, "Expected , or ] in JSON array");
        return NULL;
    }
    node->items = items;
    return node;
}

static json_value *parse_object(json_reader *r) {
    r->at++;
    json_value *node = new_value(r, JSON_OBJ);
    if (node == NULL) {
        return NULL;
    }
    size_t cap = 8;
    char **keys = (char **)arena_alloc(r->a, cap * sizeof(char *));
    json_value **values = (json_value **)arena_alloc(r->a, cap * sizeof(json_value *));
    if (keys == NULL || values == NULL) {
        err_set(r->e, "Out of memory parsing JSON object");
        return NULL;
    }
    skip_space(r);
    if (r->at < r->len && r->text[r->at] == '}') {
        r->at++;
        node->keys = keys;
        node->items = values;
        return node;
    }
    for (;;) {
        skip_space(r);
        if (r->at >= r->len || r->text[r->at] != '"') {
            err_set(r->e, "Expected a key string in JSON object");
            return NULL;
        }
        json_value *key = parse_string(r);
        if (key == NULL) {
            return NULL;
        }
        skip_space(r);
        if (r->at >= r->len || r->text[r->at] != ':') {
            err_set(r->e, "Expected : after JSON key");
            return NULL;
        }
        r->at++;
        skip_space(r);
        json_value *value = parse_value(r);
        if (value == NULL) {
            return NULL;
        }
        if (node->count == cap) {
            size_t grown = cap * 2;
            char **bigger_keys = (char **)arena_alloc(r->a, grown * sizeof(char *));
            json_value **bigger_values =
                (json_value **)arena_alloc(r->a, grown * sizeof(json_value *));
            if (bigger_keys == NULL || bigger_values == NULL) {
                err_set(r->e, "Out of memory growing JSON object");
                return NULL;
            }
            memcpy(bigger_keys, keys, cap * sizeof(char *));
            memcpy(bigger_values, values, cap * sizeof(json_value *));
            keys = bigger_keys;
            values = bigger_values;
            cap = grown;
        }
        keys[node->count] = key->text;
        values[node->count] = value;
        node->count++;
        skip_space(r);
        if (r->at < r->len && r->text[r->at] == ',') {
            r->at++;
            continue;
        }
        if (r->at < r->len && r->text[r->at] == '}') {
            r->at++;
            break;
        }
        err_set(r->e, "Expected , or } in JSON object");
        return NULL;
    }
    node->keys = keys;
    node->items = values;
    return node;
}

static json_value *parse_value(json_reader *r) {
    skip_space(r);
    if (r->at >= r->len) {
        err_set(r->e, "Unexpected end of JSON");
        return NULL;
    }
    char ch = r->text[r->at];
    if (ch == '{') {
        return parse_object(r);
    }
    if (ch == '[') {
        return parse_array(r);
    }
    if (ch == '"') {
        return parse_string(r);
    }
    if (r->len - r->at >= 4 && memcmp(r->text + r->at, "true", 4) == 0) {
        r->at += 4;
        json_value *node = new_value(r, JSON_BOOL);
        if (node != NULL) {
            node->boolean = 1;
        }
        return node;
    }
    if (r->len - r->at >= 5 && memcmp(r->text + r->at, "false", 5) == 0) {
        r->at += 5;
        return new_value(r, JSON_BOOL);
    }
    if (r->len - r->at >= 4 && memcmp(r->text + r->at, "null", 4) == 0) {
        r->at += 4;
        return new_value(r, JSON_NULL);
    }
    if ((ch >= '0' && ch <= '9') || ch == '-') {
        return parse_number(r);
    }
    err_set(r->e, "Unexpected character '%c' in JSON", ch);
    return NULL;
}

json_value *json_parse(arena *a, const char *text, size_t len, err *e) {
    json_reader r;
    r.text = text;
    r.len = len;
    r.at = 0;
    r.a = a;
    r.e = e;
    json_value *node = parse_value(&r);
    return node;
}

json_value *json_obj_get(const json_value *node, const char *key) {
    if (node == NULL || node->kind != JSON_OBJ) {
        return NULL;
    }
    for (size_t i = 0; i < node->count; i++) {
        if (node->keys[i] != NULL && strcmp(node->keys[i], key) == 0) {
            return node->items[i];
        }
    }
    return NULL;
}

const char *json_as_str(const json_value *node, const char *fallback) {
    if (node == NULL || node->kind != JSON_STR) {
        return fallback;
    }
    return node->text;
}

int64_t json_as_i64(const json_value *node, int64_t fallback) {
    if (node == NULL) {
        return fallback;
    }
    if (node->kind == JSON_NUM) {
        return (int64_t)node->number;
    }
    if (node->kind == JSON_BOOL) {
        return node->boolean ? 1 : 0;
    }
    if (node->kind == JSON_STR && node->text != NULL) {
        return (int64_t)strtoll(node->text, NULL, 10);
    }
    return fallback;
}

int json_as_bool(const json_value *node, int fallback) {
    if (node == NULL) {
        return fallback;
    }
    if (node->kind == JSON_BOOL) {
        return node->boolean;
    }
    if (node->kind == JSON_NUM) {
        return node->number != 0.0;
    }
    return fallback;
}

const json_value *json_at(const json_value *node, size_t index) {
    if (node == NULL || node->items == NULL || index >= node->count) {
        return NULL;
    }
    return node->items[index];
}

size_t json_count(const json_value *node) {
    return node == NULL ? 0 : node->count;
}

void jw_init(json_writer *w, FILE *sink) {
    buf_init(&w->out);
    w->sink = sink;
    w->depth = 0;
    w->failed = 0;
    w->has_item[0] = 0;
}

void jw_free(json_writer *w) {
    buf_free(&w->out);
}

static void jw_flush_if_big(json_writer *w) {
    if (w->sink == NULL || w->out.len < JSON_FLUSH_AT) {
        return;
    }
    if (fwrite(w->out.data, 1, w->out.len, w->sink) != w->out.len) {
        w->failed = 1;
    }
    buf_reset(&w->out);
}

static void jw_prefix(json_writer *w) {
    if (w->depth > 0 && w->has_item[w->depth]) {
        if (!buf_putc(&w->out, ',')) {
            w->failed = 1;
        }
    }
    if (w->depth > 0) {
        w->has_item[w->depth] = 1;
    }
}

static void jw_push(json_writer *w, char open) {
    jw_prefix(w);
    if (!buf_putc(&w->out, open)) {
        w->failed = 1;
    }
    if (w->depth + 1 < JSON_DEPTH_MAX) {
        w->depth++;
        w->has_item[w->depth] = 0;
    } else {
        w->failed = 1;
    }
    jw_flush_if_big(w);
}

static void jw_pop(json_writer *w, char close) {
    if (w->depth > 0) {
        w->depth--;
    }
    if (!buf_putc(&w->out, close)) {
        w->failed = 1;
    }
    jw_flush_if_big(w);
}

void jw_obj_open(json_writer *w) {
    jw_push(w, '{');
}

void jw_obj_close(json_writer *w) {
    jw_pop(w, '}');
}

void jw_arr_open(json_writer *w) {
    jw_push(w, '[');
}

void jw_arr_close(json_writer *w) {
    jw_pop(w, ']');
}

static void jw_escaped(json_writer *w, const char *text, size_t len) {
    static const char digits[] = "0123456789abcdef";
    if (!buf_putc(&w->out, '"')) {
        w->failed = 1;
        return;
    }
    size_t i = 0;
    while (i < len) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == '"' || ch == '\\') {
            char pair[2] = {'\\', (char)ch};
            buf_put(&w->out, pair, 2);
            i++;
            continue;
        }
        if (ch == '\n') { buf_put(&w->out, "\\n", 2); i++; continue; }
        if (ch == '\r') { buf_put(&w->out, "\\r", 2); i++; continue; }
        if (ch == '\t') { buf_put(&w->out, "\\t", 2); i++; continue; }
        if (ch == '\b') { buf_put(&w->out, "\\b", 2); i++; continue; }
        if (ch == '\f') { buf_put(&w->out, "\\f", 2); i++; continue; }
        if (ch < 0x20u) {
            char esc[6] = {'\\', 'u', '0', '0', digits[ch >> 4], digits[ch & 15]};
            buf_put(&w->out, esc, 6);
            i++;
            continue;
        }
        if (ch < 0x80u) {
            buf_putc(&w->out, (char)ch);
            i++;
            continue;
        }
        size_t need = 0;
        if ((ch & 0xE0u) == 0xC0u) {
            need = 2;
        } else if ((ch & 0xF0u) == 0xE0u) {
            need = 3;
        } else if ((ch & 0xF8u) == 0xF0u) {
            need = 4;
        }
        int valid = need > 0 && i + need <= len;
        if (valid) {
            for (size_t k = 1; k < need; k++) {
                if (((unsigned char)text[i + k] & 0xC0u) != 0x80u) {
                    valid = 0;
                    break;
                }
            }
        }
        if (valid) {
            buf_put(&w->out, text + i, need);
            i += need;
        } else {
            buf_put(&w->out, "\\ufffd", 6);
            i++;
        }
    }
    if (!buf_putc(&w->out, '"')) {
        w->failed = 1;
    }
    jw_flush_if_big(w);
}

void jw_key_n(json_writer *w, const char *key, size_t len) {
    jw_prefix(w);
    if (w->depth > 0) {
        w->has_item[w->depth] = 0;
    }
    jw_escaped(w, key, len);
    if (!buf_putc(&w->out, ':')) {
        w->failed = 1;
    }
}

void jw_key(json_writer *w, const char *key) {
    jw_key_n(w, key, strlen(key));
}

void jw_strn(json_writer *w, const char *text, size_t len) {
    jw_prefix(w);
    jw_escaped(w, text, len);
}

void jw_str(json_writer *w, const char *text) {
    jw_strn(w, text == NULL ? "" : text, text == NULL ? 0 : strlen(text));
}

void jw_i64(json_writer *w, int64_t value) {
    jw_prefix(w);
    if (!buf_printf(&w->out, "%lld", (long long)value)) {
        w->failed = 1;
    }
    jw_flush_if_big(w);
}

void jw_u64(json_writer *w, uint64_t value) {
    jw_prefix(w);
    if (!buf_printf(&w->out, "%llu", (unsigned long long)value)) {
        w->failed = 1;
    }
    jw_flush_if_big(w);
}

void jw_bool(json_writer *w, int value) {
    jw_prefix(w);
    if (!buf_puts(&w->out, value ? "true" : "false")) {
        w->failed = 1;
    }
}

void jw_null(json_writer *w) {
    jw_prefix(w);
    if (!buf_puts(&w->out, "null")) {
        w->failed = 1;
    }
}

void jw_kv_str(json_writer *w, const char *key, const char *text) {
    jw_key(w, key);
    jw_str(w, text);
}

void jw_kv_strn(json_writer *w, const char *key, const char *text, size_t len) {
    jw_key(w, key);
    jw_strn(w, text, len);
}

void jw_kv_i64(json_writer *w, const char *key, int64_t value) {
    jw_key(w, key);
    jw_i64(w, value);
}

void jw_kv_u64(json_writer *w, const char *key, uint64_t value) {
    jw_key(w, key);
    jw_u64(w, value);
}

void jw_kv_bool(json_writer *w, const char *key, int value) {
    jw_key(w, key);
    jw_bool(w, value);
}

int jw_finish(json_writer *w) {
    if (w->sink != NULL && w->out.len > 0) {
        if (fwrite(w->out.data, 1, w->out.len, w->sink) != w->out.len) {
            w->failed = 1;
        }
        buf_reset(&w->out);
    }
    return w->failed ? 0 : 1;
}
