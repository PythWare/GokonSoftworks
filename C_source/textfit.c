#include "textfit.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

void lines_init(ui_lines *lines) {
    lines->items = NULL;
    lines->count = 0;
    lines->cap = 0;
}

int lines_push(ui_lines *lines, const wchar_t *text, int len) {
    if (lines->count == lines->cap) {
        int grown = lines->cap == 0 ? 8 : lines->cap * 2;
        wchar_t **bigger = (wchar_t **)realloc(lines->items, (size_t)grown * sizeof(wchar_t *));
        if (bigger == NULL) {
            return 0;
        }
        lines->items = bigger;
        lines->cap = grown;
    }
    wchar_t *copy = (wchar_t *)malloc(((size_t)len + 1) * sizeof(wchar_t));
    if (copy == NULL) {
        return 0;
    }
    if (len > 0) {
        memcpy(copy, text, (size_t)len * sizeof(wchar_t));
    }
    copy[len] = 0;
    lines->items[lines->count++] = copy;
    return 1;
}

void lines_free(ui_lines *lines) {
    for (int i = 0; i < lines->count; i++) {
        free(lines->items[i]);
    }
    free(lines->items);
    lines_init(lines);
}

int fit_cut(ui_font *font, const wchar_t *text, int len, float max_width) {
    int bound = (int)max_width;
    if (bound < 1) {
        bound = 1;
    }
    int low = 1;
    int high = len < bound ? len : bound;
    while (low < high) {
        int middle = (low + high + 1) / 2;
        if (ui_measure(font, text, middle) <= max_width) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return low;
}

static int wrap_one(ui_font *font, const wchar_t *text, int len, float max_width, int start,
                    int *line_start, int *line_len) {
    int index = start;
    while (index < len && text[index] == L' ') {
        index++;
    }
    if (index >= len) {
        *line_start = len;
        *line_len = 0;
        return len;
    }
    int begin = index;
    int bound = (int)max_width;
    int ceiling = begin + (bound < 1 ? 1 : bound);
    int last_fit = -1;
    int cursor = index;
    while (cursor < len && cursor <= ceiling) {
        int word_end = cursor;
        while (word_end < len && text[word_end] != L' ' && word_end <= ceiling) {
            word_end++;
        }
        if (ui_measure(font, text + begin, word_end - begin) > max_width) {
            break;
        }
        last_fit = word_end;
        while (word_end < len && text[word_end] == L' ') {
            word_end++;
        }
        cursor = word_end;
    }
    *line_start = begin;
    if (last_fit < 0) {
        int span = (ceiling < len ? ceiling : len) - begin;
        int cut = fit_cut(font, text + begin, span, max_width);
        *line_len = cut;
        return begin + cut;
    }
    *line_len = last_fit - begin;
    return last_fit;
}

static int is_strip_space(wchar_t ch) {
    return ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\r' || ch == L'\v' || ch == L'\f';
}

wchar_t *fit_line(ui_font *font, const wchar_t *text, int len, float max_width) {
    wchar_t *out = (wchar_t *)malloc(((size_t)len + 4) * sizeof(wchar_t));
    if (out == NULL) {
        return NULL;
    }
    if (max_width <= 0.0f || ui_measure(font, text, len) <= max_width) {
        memcpy(out, text, (size_t)len * sizeof(wchar_t));
        out[len] = 0;
        return out;
    }
    float budget = max_width - ui_measure(font, L"...", 3);
    if (budget <= 0.0f) {
        wcscpy(out, L"...");
        return out;
    }
    int cut = fit_cut(font, text, len, budget);
    if (ui_measure(font, text, cut) > budget) {
        cut = 0;
    }
    while (cut > 0 && is_strip_space(text[cut - 1])) {
        cut--;
    }
    memcpy(out, text, (size_t)cut * sizeof(wchar_t));
    wcscpy(out + cut, L"...");
    return out;
}

static int line_break_at(const wchar_t *text, int len, int at, int *width) {
    wchar_t ch = text[at];
    if (ch == L'\r' && at + 1 < len && text[at + 1] == L'\n') {
        *width = 2;
        return 1;
    }
    *width = 1;
    return ch == L'\n' || ch == L'\r' || ch == L'\v' || ch == L'\f' || ch == 0x1C || ch == 0x1D ||
           ch == 0x1E || ch == 0x85 || ch == 0x2028 || ch == 0x2029;
}

void wrap_lines(ui_font *font, const wchar_t *text, int len, float max_width, int max_lines, ui_lines *out) {
    int at = 0;
    int any = 0;
    while (at < len || !any) {
        int start = at;
        int stop = at;
        int width = 0;
        while (stop < len && !line_break_at(text, len, stop, &width)) {
            stop++;
        }
        int next = stop < len ? stop + width : len;
        any = 1;
        const wchar_t *paragraph = text + start;
        int plen = stop - start;
        int cursor = 0;
        if (plen == 0) {
            lines_push(out, L"", 0);
        }
        while (cursor < plen) {
            int line_start = 0;
            int line_len = 0;
            cursor = wrap_one(font, paragraph, plen, max_width, cursor, &line_start, &line_len);
            lines_push(out, paragraph + line_start, line_len);
            if (max_lines > 0 && out->count >= max_lines) {
                if (cursor < plen) {
                    wchar_t *last = out->items[out->count - 1];
                    size_t last_len = wcslen(last);
                    wchar_t *extended = (wchar_t *)malloc((last_len + 4) * sizeof(wchar_t));
                    if (extended != NULL) {
                        wcscpy(extended, last);
                        wcscat(extended, L"...");
                        wchar_t *fitted = fit_line(font, extended, (int)last_len + 3, max_width);
                        free(extended);
                        if (fitted != NULL) {
                            free(last);
                            out->items[out->count - 1] = fitted;
                        }
                    }
                }
                while (out->count > max_lines) {
                    free(out->items[--out->count]);
                }
                return;
            }
        }
        at = next;
        if (at >= len) {
            break;
        }
    }
}

wchar_t *join_lines(const ui_lines *lines) {
    size_t total = 1;
    for (int i = 0; i < lines->count; i++) {
        total += wcslen(lines->items[i]) + 1;
    }
    wchar_t *out = (wchar_t *)malloc(total * sizeof(wchar_t));
    if (out == NULL) {
        return NULL;
    }
    out[0] = 0;
    for (int i = 0; i < lines->count; i++) {
        if (i > 0) {
            wcscat(out, L"\n");
        }
        wcscat(out, lines->items[i]);
    }
    return out;
}
