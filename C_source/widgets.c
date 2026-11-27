#include "widgets.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static const float glass_interior[28] = {
    389, 26, 345, 85, 303, 129, 230, 188, 220, 194,
    181, 196, 142, 171, 109, 143, 88, 123, 66, 100,
    43, 75, 28, 53, 12, 29, 388, 26,
};

static float py_round(double value) {
    return (float)nearbyint(value);
}

uint32_t ink_colour(const gs_theme *theme, ui_ink ink) {
    switch (ink) {
        case INK_TEXT: return theme->text;
        case INK_OK: return theme->ok;
        case INK_DANGER: return theme->danger;
        case INK_ACCENT: return theme->accent;
        default: return theme->text_muted;
    }
}

void panel_init(ui_panel *p, float x, float y, float w, float h, const char *title) {
    memset(p, 0, sizeof(*p));
    p->x = x;
    p->y = y;
    p->w = w;
    p->h = h;
    p->title = title != NULL && title[0] != 0 ? ui_wide(title) : NULL;
}

void panel_draw(ui_window *win, const ui_panel *p) {
    const gs_theme *t = win->theme;
    ui_fill_rect(win, p->x, p->y, p->x + p->w, p->y + p->h, p->has_fill ? p->fill : t->panel);
    ui_stroke_rect(win, p->x, p->y, p->x + p->w + 1, p->y + p->h + 1, t->panel_soft, 1.0f);
    if (p->title != NULL) {
        float wrap = p->w - 28 > 10 ? p->w - 28 : 10;
        ui_text(win, p->x + 14, p->y + 12, p->title, (int)wcslen(p->title),
                ui_font_get(L"Segoe UI", 11, 1), t->text, UI_NW, wrap, 0);
    }
}

void button_init(ui_button *b, float x, float y, float w, float h, const char *label,
                 ui_action command, void *ctx, ui_tone tone) {
    memset(b, 0, sizeof(*b));
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
    b->label = ui_wide(label);
    b->command = command;
    b->ctx = ctx;
    b->tone = tone;
    b->enabled = 1;
}

void button_free(ui_button *b) {
    free(b->label);
    b->label = NULL;
}

void button_set_text(ui_button *b, const char *label) {
    free(b->label);
    b->label = ui_wide(label);
}

static uint32_t button_edge(const gs_theme *t, const ui_button *b) {
    if (b->tone == TONE_DANGER) {
        return t->danger;
    }
    if (b->tone == TONE_ACCENT) {
        return t->accent;
    }
    return t->panel_soft;
}

static uint32_t button_hover_fill(const gs_theme *t, const ui_button *b) {
    if (b->tone == TONE_DANGER) {
        return t->danger;
    }
    if (b->tone == TONE_ACCENT) {
        return t->accent;
    }
    return t->text_muted;
}

void button_draw(ui_window *win, const ui_button *b) {
    const gs_theme *t = win->theme;
    uint32_t fill = t->panel_soft;
    uint32_t edge = button_edge(t, b);
    uint32_t ink = t->text;
    if (!b->enabled) {
        fill = t->panel;
        edge = t->panel_soft;
        ink = t->text_muted;
    } else if (b->hovering) {
        fill = button_hover_fill(t, b);
        ink = t->bg;
    }
    ui_fill_rect(win, b->x, b->y, b->x + b->w, b->y + b->h, fill);
    ui_stroke_rect(win, b->x, b->y, b->x + b->w + 1, b->y + b->h + 1, edge, 1.0f);
    ui_font *font = ui_font_get(L"Segoe UI", 10, 1);
    wchar_t *shown = fit_line(font, b->label, (int)wcslen(b->label), b->w - 18);
    if (shown != NULL) {
        float wrap = b->w - 18 > 10 ? b->w - 18 : 10;
        ui_text(win, b->x + b->w / 2, b->y + b->h / 2, shown, (int)wcslen(shown), font, ink, UI_CENTER, wrap, 1);
        free(shown);
    }
}

int button_hit(const ui_button *b, float x, float y) {
    return x >= b->x && x <= b->x + b->w && y >= b->y && y <= b->y + b->h;
}

int buttons_move(ui_button **list, int count, float x, float y) {
    int changed = 0;
    for (int i = 0; i < count; i++) {
        int hover = list[i]->enabled && button_hit(list[i], x, y);
        if (hover != list[i]->hovering) {
            list[i]->hovering = hover;
            changed = 1;
        }
    }
    return changed;
}

void buttons_down(ui_button **list, int count, float x, float y) {
    for (int i = 0; i < count; i++) {
        list[i]->pressed = list[i]->enabled && button_hit(list[i], x, y);
    }
}

void buttons_up(ui_button **list, int count, float x, float y) {
    ui_button *fire = NULL;
    for (int i = 0; i < count; i++) {
        if (list[i]->pressed && list[i]->enabled && button_hit(list[i], x, y) && fire == NULL) {
            fire = list[i];
        }
        list[i]->pressed = 0;
    }
    if (fire != NULL && fire->command != NULL) {
        fire->command(fire->ctx);
    }
}

void buttons_leave(ui_button **list, int count) {
    for (int i = 0; i < count; i++) {
        list[i]->hovering = 0;
    }
}

void progress_init(ui_progress *p, float x, float y, float w, float h) {
    p->x = x;
    p->y = y;
    p->w = w;
    p->h = h;
    p->fraction = 0.0f;
}

int progress_set(ui_progress *p, float fraction) {
    if (fraction < 0.0f) {
        fraction = 0.0f;
    }
    if (fraction > 1.0f) {
        fraction = 1.0f;
    }
    if (fabsf(fraction - p->fraction) < 0.001f) {
        return 0;
    }
    p->fraction = fraction;
    return 1;
}

void progress_draw(ui_window *win, const gs_theme *theme, const ui_progress *p) {
    ui_fill_rect(win, p->x, p->y, p->x + p->w, p->y + p->h, theme->field);
    uint32_t fill = p->fraction >= 1.0f ? theme->liquid_top : theme->liquid_bottom;
    ui_fill_rect(win, p->x, p->y, p->x + p->w * p->fraction, p->y + p->h, fill);
}

static int visible_count(const ui_log *log) {
    int count = (int)((log->h - 16) / log->line_height);
    return count < 1 ? 1 : count;
}

static void cache_clear(ui_log *log) {
    for (int i = 0; i < log->cache_count; i++) {
        lines_free(&log->cache[i].lines);
    }
    log->cache_count = 0;
}

static void blocks_free(ui_log *log) {
    for (int i = 0; i < log->block_count; i++) {
        free(log->blocks[i]);
    }
    free(log->blocks);
    log->blocks = NULL;
    log->block_count = 0;
}

static void lines_clear(ui_log *log) {
    for (int i = 0; i < log->line_count; i++) {
        free(log->lines[i].text);
    }
    log->line_count = 0;
}

static void messages_clear(ui_log *log) {
    for (int i = 0; i < log->message_count; i++) {
        free(log->messages[i].text);
    }
    log->message_count = 0;
}

void log_init(ui_log *log, const gs_theme *theme, float x, float y, float w, float h,
              const wchar_t *family, float points, int follow_tail) {
    memset(log, 0, sizeof(*log));
    log->theme = theme;
    log->x = x;
    log->y = y;
    log->w = w;
    log->h = h;
    log->font = ui_font_get(family, points, 0);
    log->line_height = log->font->line_space + 2;
    log->follow_tail = follow_tail;
    log->block_colour = theme->text;
    log->avg_lines = 1.0;
}

void log_free(ui_log *log) {
    cache_clear(log);
    blocks_free(log);
    lines_clear(log);
    messages_clear(log);
}

static int block_mode(const ui_log *log) {
    return log->block_count > 0;
}

static void push_block(ui_log *log, const wchar_t *text, int len) {
    wchar_t **bigger = (wchar_t **)realloc(log->blocks, (size_t)(log->block_count + 1) * sizeof(wchar_t *));
    if (bigger == NULL) {
        return;
    }
    log->blocks = bigger;
    wchar_t *copy = (wchar_t *)malloc(((size_t)len + 1) * sizeof(wchar_t));
    if (copy == NULL) {
        return;
    }
    memcpy(copy, text, (size_t)len * sizeof(wchar_t));
    copy[len] = 0;
    log->blocks[log->block_count++] = copy;
}

static void split_blocks(ui_log *log, const wchar_t *text) {
    int len = (int)wcslen(text);
    if (len == 0) {
        return;
    }
    ui_lines paragraphs;
    lines_init(&paragraphs);
    int at = 0;
    while (at < len) {
        int stop = at;
        while (stop < len && text[stop] != L'\n' && text[stop] != L'\r') {
            stop++;
        }
        lines_push(&paragraphs, text + at, stop - at);
        if (stop < len && text[stop] == L'\r' && stop + 1 < len && text[stop + 1] == L'\n') {
            stop++;
        }
        at = stop + 1;
    }
    for (int p = 0; p < paragraphs.count; p++) {
        const wchar_t *paragraph = paragraphs.items[p];
        int plen = (int)wcslen(paragraph);
        if (plen <= LOG_BLOCK_CHARS) {
            push_block(log, paragraph, plen);
            continue;
        }
        int pos = 0;
        while (pos < plen) {
            int end = pos + LOG_BLOCK_CHARS < plen ? pos + LOG_BLOCK_CHARS : plen;
            if (end < plen) {
                int space = -1;
                for (int k = end - 1; k >= pos + LOG_BLOCK_CHARS / 2; k--) {
                    if (paragraph[k] == L' ') {
                        space = k;
                        break;
                    }
                }
                if (space > pos) {
                    end = space + 1;
                }
            }
            push_block(log, paragraph + pos, end - pos);
            pos = end;
        }
    }
    lines_free(&paragraphs);
}

static ui_lines *block_wrapped(ui_log *log, int index) {
    for (int i = 0; i < log->cache_count; i++) {
        if (log->cache[i].index == index) {
            return &log->cache[i].lines;
        }
    }
    log_wrap *slot = &log->cache[log->cache_count++];
    slot->index = index;
    lines_init(&slot->lines);
    const wchar_t *source = log->blocks[index];
    int len = (int)wcslen(source);
    if (len > 0) {
        wrap_lines(log->font, source, len, log->w - 20, 0, &slot->lines);
    }
    if (slot->lines.count == 0) {
        lines_push(&slot->lines, L"", 0);
    }
    while (log->cache_count > LOG_BLOCK_CACHE) {
        lines_free(&log->cache[0].lines);
        memmove(&log->cache[0], &log->cache[1], (size_t)(log->cache_count - 1) * sizeof(log_wrap));
        log->cache_count--;
    }
    double total = 0.0;
    for (int i = 0; i < log->cache_count; i++) {
        total += log->cache[i].lines.count;
    }
    log->avg_lines = log->cache_count > 0 ? total / log->cache_count : 1.0;
    for (int i = 0; i < log->cache_count; i++) {
        if (log->cache[i].index == index) {
            return &log->cache[i].lines;
        }
    }
    return &log->cache[log->cache_count - 1].lines;
}

static void end_top(ui_log *log, int *block, int *line) {
    int need = visible_count(log);
    int seen = 0;
    for (int index = log->block_count - 1; index >= 0; index--) {
        int count = block_wrapped(log, index)->count;
        if (seen + count >= need) {
            *block = index;
            *line = count - (need - seen);
            return;
        }
        seen += count;
    }
    *block = 0;
    *line = 0;
}

static int pair_greater(int a, int b, int c, int d) {
    return a > c || (a == c && b > d);
}

static void clamp_top(ui_log *log) {
    int last_block = 0;
    int last_line = 0;
    end_top(log, &last_block, &last_line);
    if (pair_greater(log->top_block, log->top_line, last_block, last_line)) {
        log->top_block = last_block;
        log->top_line = last_line;
    }
    if (log->top_block < 0) {
        log->top_block = 0;
        log->top_line = 0;
    }
}

static void step_lines(ui_log *log, int delta) {
    int block = log->top_block;
    int line = log->top_line;
    while (delta > 0) {
        int count = block_wrapped(log, block)->count;
        if (line + 1 < count) {
            line++;
        } else if (block + 1 < log->block_count) {
            block++;
            line = 0;
        } else {
            break;
        }
        delta--;
    }
    while (delta < 0) {
        if (line > 0) {
            line--;
        } else if (block > 0) {
            block--;
            line = block_wrapped(log, block)->count - 1;
        } else {
            break;
        }
        delta++;
    }
    log->top_block = block;
    log->top_line = line;
    clamp_top(log);
}

static int max_offset(const ui_log *log) {
    int value = log->line_count - visible_count(log);
    return value > 0 ? value : 0;
}

static void refresh(ui_log *log) {
    if (!block_mode(log) && log->offset > max_offset(log)) {
        log->offset = max_offset(log);
    }
}

static void add_wrapped(ui_log *log, const wchar_t *text, uint32_t colour) {
    ui_lines wrapped;
    lines_init(&wrapped);
    wrap_lines(log->font, text, (int)wcslen(text), log->w - 20, 0, &wrapped);
    for (int i = 0; i < wrapped.count; i++) {
        if (log->line_count >= LOG_MAX_LINES + 63) {
            free(log->lines[0].text);
            memmove(&log->lines[0], &log->lines[1], (size_t)(log->line_count - 1) * sizeof(log_line));
            log->line_count--;
        }
        log->lines[log->line_count].text = wrapped.items[i];
        log->lines[log->line_count].colour = colour;
        log->line_count++;
        wrapped.items[i] = NULL;
    }
    free(wrapped.items);
}

static void trim_lines(ui_log *log) {
    if (log->line_count > LOG_MAX_LINES) {
        int drop = log->line_count - LOG_MAX_LINES;
        for (int i = 0; i < drop; i++) {
            free(log->lines[i].text);
        }
        memmove(&log->lines[0], &log->lines[drop], (size_t)LOG_MAX_LINES * sizeof(log_line));
        log->line_count = LOG_MAX_LINES;
    }
}

void log_set_text(ui_log *log, const char *text, ui_ink ink) {
    uint32_t colour = ink_colour(log->theme, ink);
    messages_clear(log);
    wchar_t *wide = ui_wide(text);
    if (wide != NULL && wide[0] != 0) {
        log->messages[0].text = wide;
        log->messages[0].colour = colour;
        log->message_count = 1;
    }
    log->block_colour = colour;
    lines_clear(log);
    log->offset = 0;
    cache_clear(log);
    blocks_free(log);
    if (wide != NULL) {
        split_blocks(log, wide);
    }
    if (log->message_count == 0) {
        free(wide);
    }
    log->top_block = 0;
    log->top_line = 0;
    log->avg_lines = 1.0;
    refresh(log);
}

void log_clear(ui_log *log) {
    log_set_text(log, "", INK_TEXT);
}

void log_write(ui_log *log, const char *text, ui_ink ink) {
    uint32_t colour = ink_colour(log->theme, ink);
    wchar_t *wide = ui_wide(text);
    if (wide == NULL) {
        return;
    }
    if (log->message_count >= LOG_MAX_LINES) {
        free(log->messages[0].text);
        memmove(&log->messages[0], &log->messages[1], (size_t)(log->message_count - 1) * sizeof(log_line));
        log->message_count--;
    }
    log->messages[log->message_count].text = wide;
    log->messages[log->message_count].colour = colour;
    log->message_count++;
    blocks_free(log);
    cache_clear(log);
    add_wrapped(log, wide, colour);
    trim_lines(log);
    log->offset = max_offset(log);
    refresh(log);
}

static void rewrap(ui_log *log) {
    if (block_mode(log)) {
        cache_clear(log);
        clamp_top(log);
        return;
    }
    lines_clear(log);
    for (int i = 0; i < log->message_count; i++) {
        add_wrapped(log, log->messages[i].text, log->messages[i].colour);
    }
    trim_lines(log);
}

void log_place(ui_log *log, float x, float y, float w, float h) {
    int must_rewrap = w != log->w;
    log->x = x;
    log->y = y;
    log->w = w;
    log->h = h;
    if (must_rewrap) {
        rewrap(log);
    }
    if (block_mode(log)) {
        clamp_top(log);
    } else {
        int limit = max_offset(log);
        log->offset = log->follow_tail ? limit : (log->offset < limit ? log->offset : limit);
    }
    refresh(log);
}

static void track_bounds(const ui_log *log, float *l, float *t, float *r, float *b) {
    float left = log->x + log->w - LOG_BAR_WIDTH - 3;
    *l = left;
    *t = log->y + 4;
    *r = left + LOG_BAR_WIDTH;
    *b = log->y + log->h - 4;
}

static int overflowing(ui_log *log) {
    if (block_mode(log)) {
        int block = 0;
        int line = 0;
        end_top(log, &block, &line);
        return pair_greater(block, line, 0, 0);
    }
    return max_offset(log) > 0;
}

static double scroll_fraction(ui_log *log) {
    if (!block_mode(log)) {
        int limit = max_offset(log);
        return limit ? (double)log->offset / limit : 0.0;
    }
    int last_block = 0;
    int last_line = 0;
    end_top(log, &last_block, &last_line);
    if (last_block == 0 && last_line == 0) {
        return 0.0;
    }
    int here_count = block_wrapped(log, log->top_block)->count;
    double here = log->top_block + (double)log->top_line / (here_count > 1 ? here_count : 1);
    int there_count = block_wrapped(log, last_block)->count;
    double there = last_block + (double)last_line / (there_count > 1 ? there_count : 1);
    if (there == 0.0) {
        return 0.0;
    }
    double f = here / there;
    return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
}

static int thumb_bounds(ui_log *log, float *l, float *t, float *r, float *b) {
    if (!overflowing(log)) {
        return 0;
    }
    float left, top, right, bottom;
    track_bounds(log, &left, &top, &right, &bottom);
    float span = bottom - top;
    double total = block_mode(log) ? log->avg_lines * log->block_count : log->line_count;
    if (total < 1.0) {
        total = 1.0;
    }
    double portion = visible_count(log) / total;
    if (portion > 1.0) {
        portion = 1.0;
    }
    float height = (float)(span * portion);
    if (height < LOG_THUMB_MIN) {
        height = LOG_THUMB_MIN;
    }
    float travel = span - height;
    float start = top + travel * (float)scroll_fraction(log);
    *l = left;
    *t = start;
    *r = right;
    *b = start + height;
    return 1;
}

static int hit_track(ui_log *log, float px, float py) {
    if (!overflowing(log)) {
        return 0;
    }
    float left, top, right, bottom;
    track_bounds(log, &left, &top, &right, &bottom);
    return left - 3 <= px && px <= right + 3 && top <= py && py <= bottom;
}

static void scroll_to_fraction(ui_log *log, double fraction) {
    if (block_mode(log)) {
        int last_block = 0;
        int last_line = 0;
        end_top(log, &last_block, &last_line);
        double target = fraction * (last_block + (last_line ? 1 : 0));
        int block = (int)nearbyint(target);
        if (block > log->block_count - 1) {
            block = log->block_count - 1;
        }
        if (block < 0) {
            block = 0;
        }
        log->top_block = block;
        log->top_line = 0;
        clamp_top(log);
    } else {
        log->offset = (int)nearbyint(max_offset(log) * fraction);
    }
    refresh(log);
}

int log_begin_drag(ui_log *log, float px, float py) {
    float l, t, r, b;
    if (!thumb_bounds(log, &l, &t, &r, &b)) {
        return 0;
    }
    if (l - 3 <= px && px <= r + 3 && t <= py && py <= b) {
        log->dragging = 1;
        log->drag_grab = py - t;
        return 1;
    }
    if (hit_track(log, px, py)) {
        log->dragging = 1;
        log->drag_grab = (b - t) / 2;
        log_drag_to(log, py);
        return 1;
    }
    return 0;
}

void log_drag_to(ui_log *log, float py) {
    float l, t, r, b;
    if (!thumb_bounds(log, &l, &t, &r, &b)) {
        return;
    }
    float tl, tt, tr, tb;
    track_bounds(log, &tl, &tt, &tr, &tb);
    float travel = (tb - tt) - (b - t);
    if (travel <= 0) {
        return;
    }
    double fraction = (py - log->drag_grab - tt) / travel;
    fraction = fraction < 0.0 ? 0.0 : (fraction > 1.0 ? 1.0 : fraction);
    scroll_to_fraction(log, fraction);
}

void log_end_drag(ui_log *log) {
    log->dragging = 0;
}

int log_wheel(ui_log *log, float px, float py, int delta) {
    int inside = log->x <= px && px <= log->x + log->w && log->y <= py && py <= log->y + log->h;
    if (!inside) {
        return 0;
    }
    int step = delta > 0 ? 3 : -3;
    if (block_mode(log)) {
        if (!overflowing(log)) {
            return 1;
        }
        step_lines(log, -step);
    } else {
        if (max_offset(log) == 0) {
            return 1;
        }
        int offset = log->offset - step;
        int limit = max_offset(log);
        log->offset = offset < 0 ? 0 : (offset > limit ? limit : offset);
    }
    refresh(log);
    return 1;
}

void log_draw(ui_window *win, ui_log *log) {
    const gs_theme *t = log->theme;
    ui_fill_rect(win, log->x, log->y, log->x + log->w, log->y + log->h, t->field);
    ui_stroke_rect(win, log->x, log->y, log->x + log->w + 1, log->y + log->h + 1, t->panel_soft, 1.0f);
    int room = visible_count(log);
    if (!block_mode(log)) {
        refresh(log);
        for (int i = 0; i < room && log->offset + i < log->line_count; i++) {
            const log_line *line = &log->lines[log->offset + i];
            ui_text(win, log->x + 10, log->y + 8 + i * log->line_height, line->text, (int)wcslen(line->text),
                    log->font, line->colour, UI_NW, 0, 0);
        }
    } else {
        int shown = 0;
        int block = log->top_block;
        int line = log->top_line;
        while (block < log->block_count && shown < room) {
            ui_lines *lines = block_wrapped(log, block);
            while (line < lines->count && shown < room) {
                const wchar_t *text = lines->items[line];
                ui_text(win, log->x + 10, log->y + 8 + shown * log->line_height, text, (int)wcslen(text),
                        log->font, log->block_colour, UI_NW, 0, 0);
                line++;
                shown++;
            }
            block++;
            line = 0;
        }
    }
    float l, tp, r, b;
    if (thumb_bounds(log, &l, &tp, &r, &b)) {
        float tl, tt, tr, tb;
        track_bounds(log, &tl, &tt, &tr, &tb);
        ui_fill_rect(win, tl, tt, tr, tb, t->panel);
        ui_fill_rect(win, l, tp, r, b, log->dragging ? t->accent : t->text_muted);
    }
}

int gauge_init(ui_gauge *g, float x, float y, float height, int resource_id) {
    memset(g, 0, sizeof(*g));
    g->source = ui_image_resource(resource_id);
    if (g->source == NULL) {
        return 0;
    }
    float width = py_round((double)g->source->width * height / g->source->height);
    g->width = width < 1 ? 1 : width;
    g->height = height;
    double scale_x = g->width / 400.0;
    double scale_y = g->height / 500.0;
    float top = 1e9f;
    float bottom = -1e9f;
    for (int i = 0; i < 28; i++) {
        g->polygon[i] = py_round(glass_interior[i] * (i % 2 == 0 ? scale_x : scale_y));
        if (i % 2 == 1) {
            top = g->polygon[i] < top ? g->polygon[i] : top;
            bottom = g->polygon[i] > bottom ? g->polygon[i] : bottom;
        }
    }
    g->top_y = top;
    g->bottom_y = bottom;
    float arc = py_round(4 * scale_y);
    g->arc = arc < 1 ? 1 : arc;
    float limit = py_round(8 * scale_y);
    g->fill_limit = top + (limit < 2 ? 2 : limit);
    g->x = x;
    g->y = y;
    return 1;
}

void gauge_free(ui_gauge *g) {
    ui_image_free(g->source);
    ui_image_free(g->glass);
    free(g->caption);
    memset(g, 0, sizeof(*g));
}

void gauge_rescale(ui_gauge *g, float scale) {
    if (g->source == NULL || (g->glass != NULL && g->glass_scale == scale)) {
        return;
    }
    ui_image_free(g->glass);
    g->glass = ui_image_scaled(g->source, (int)(g->width * scale + 0.5f), (int)(g->height * scale + 0.5f));
    g->glass_scale = scale;
}

static int gauge_steps(const ui_gauge *g) {
    int steps = (int)(g->bottom_y - g->fill_limit);
    return steps < 1 ? 1 : steps;
}

int gauge_set(ui_gauge *g, float fraction) {
    if (fraction < 0.0f) {
        fraction = 0.0f;
    }
    if (fraction > 1.0f) {
        fraction = 1.0f;
    }
    int level = (int)nearbyint((double)fraction * gauge_steps(g));
    if (level == g->level) {
        return 0;
    }
    g->level = level;
    return 1;
}

void gauge_caption(ui_gauge *g, const char *text, int ok) {
    free(g->caption);
    g->caption = text != NULL && text[0] != 0 ? ui_wide(text) : NULL;
    g->caption_ok = ok;
}

void gauge_place(ui_gauge *g, float x, float y) {
    g->x = x;
    g->y = y;
}

void gauge_draw(ui_window *win, ui_gauge *g) {
    const gs_theme *t = win->theme;
    gauge_rescale(g, win->scale);
    float ox = g->x;
    float oy = g->y;
    float mid_y = g->bottom_y - (float)(int)((g->bottom_y - g->top_y) * 0.35f);
    float surface = g->bottom_y - (float)g->level;
    float shifted[28];
    for (int i = 0; i < 14; i++) {
        shifted[i * 2] = g->polygon[i * 2] + ox;
        shifted[i * 2 + 1] = g->polygon[i * 2 + 1] + oy;
    }
    if (ui_push_polygon_clip(win, shifted, 14)) {
        float bottom_top = surface > mid_y ? surface : mid_y;
        if (bottom_top < g->bottom_y) {
            ui_fill_rect(win, ox, oy + bottom_top, ox + g->width + 1, oy + g->bottom_y + 1, t->liquid_bottom);
            if (bottom_top == mid_y) {
                ui_fill_half_ellipse(win, ox, oy + bottom_top - g->arc, ox + g->width + 1,
                                     oy + bottom_top + g->arc + 1, t->liquid_bottom);
            }
        }
        if (surface < mid_y) {
            ui_fill_rect(win, ox, oy + surface, ox + g->width + 1, oy + mid_y + 1, t->liquid_top);
            ui_fill_half_ellipse(win, ox, oy + surface - g->arc, ox + g->width + 1, oy + surface + g->arc + 1,
                                 t->liquid_top);
        }
        ui_pop_layer(win);
    }
    ui_draw_image(win, g->glass, ox, oy, ox + g->width, oy + g->height);
    if (g->caption != NULL) {
        ui_text(win, ox + g->width / 2, oy + g->height + 6, g->caption, (int)wcslen(g->caption),
                ui_font_get(L"Segoe UI", 8, 0), g->caption_ok ? t->ok : t->text_muted, UI_N, g->width + 40, 1);
    }
}

void menu_init(ui_menu *m, void (*on_pick)(void *ctx, const char *key), void *ctx) {
    memset(m, 0, sizeof(*m));
    m->on_pick = on_pick;
    m->ctx = ctx;
    m->hover = -1;
    m->columns = 2;
}

void menu_free(ui_menu *m) {
    for (int i = 0; i < m->row_count; i++) {
        free(m->rows[i].label);
        free(m->rows[i].note);
    }
    for (int i = 0; i < m->page_count; i++) {
        free(m->headings[i]);
    }
    m->row_count = 0;
    m->page_count = 0;
}

void menu_add_page(ui_menu *m, const char *heading) {
    if (m->page_count < MENU_MAX_PAGES) {
        m->headings[m->page_count++] = ui_wide(heading);
    }
}

void menu_add_row(ui_menu *m, const char *key, const char *label, const char *note) {
    if (m->row_count >= MENU_MAX_ROWS || m->page_count == 0) {
        return;
    }
    menu_row *row = &m->rows[m->row_count++];
    memset(row, 0, sizeof(*row));
    snprintf(row->key, sizeof(row->key), "%s", key);
    row->label = ui_wide(label);
    row->note = ui_wide(note);
    row->page = m->page_count - 1;
}

int menu_rows_per_page(const ui_menu *m) {
    int best = 0;
    for (int p = 0; p < m->page_count; p++) {
        int count = 0;
        for (int i = 0; i < m->row_count; i++) {
            count += m->rows[i].page == p;
        }
        best = count > best ? count : best;
    }
    return best;
}

static void menu_layout(ui_menu *m) {
    int shown = 0;
    for (int i = 0; i < m->row_count; i++) {
        shown += m->rows[i].page == m->page;
    }
    int columns = m->columns < 1 ? 1 : m->columns;
    int per_column = (shown + columns - 1) / columns;
    if (per_column < 1) {
        per_column = 1;
    }
    float column_width = m->width / columns;
    int index = 0;
    for (int i = 0; i < m->row_count; i++) {
        menu_row *row = &m->rows[i];
        if (row->page != m->page) {
            row->x0 = row->y0 = row->x1 = row->y1 = -4000;
            continue;
        }
        int column = index / per_column;
        int line = index % per_column;
        float left = m->x + column * column_width;
        float top = m->y + line * MENU_ROW_HEIGHT;
        row->x0 = left;
        row->x1 = left + column_width - 24;
        row->y0 = top - 11;
        row->y1 = top + 11;
        index++;
    }
}

void menu_select(ui_menu *m, const char *key) {
    snprintf(m->selected, sizeof(m->selected), "%s", key);
    for (int i = 0; i < m->row_count; i++) {
        if (strcmp(m->rows[i].key, key) == 0) {
            m->page = m->rows[i].page;
            break;
        }
    }
    menu_layout(m);
}

void menu_place(ui_menu *m, float x, float y, float width, int columns) {
    m->x = x;
    m->y = y;
    m->width = width;
    m->columns = columns;
    m->placed = 1;
    menu_layout(m);
}

static void dot_centre(const ui_menu *m, int index, float *cx, float *cy) {
    int columns = m->columns < 1 ? 1 : m->columns;
    int rows_down = (menu_rows_per_page(m) + columns - 1) / columns;
    float bottom = m->y + rows_down * MENU_ROW_HEIGHT + 14;
    int count = m->page_count - 1 > 1 ? m->page_count - 1 : 1;
    float left = m->x + MENU_DOT_INSET;
    float right = m->x + m->width - MENU_DOT_INSET;
    *cx = m->page_count < 2 ? left : left + index * (right - left) / count;
    *cy = bottom;
}

void menu_draw(ui_window *win, ui_menu *m) {
    const gs_theme *t = win->theme;
    if (m->page_count > 0) {
        wchar_t *heading = m->headings[m->page];
        ui_text(win, m->x + m->width / 2, m->y - 44, heading, (int)wcslen(heading),
                ui_font_get(L"Segoe UI", 10, 1), t->text_muted, UI_N, 0, 0);
    }
    int columns = m->columns < 1 ? 1 : m->columns;
    float column_width = m->width / columns;
    for (int i = 0; i < m->row_count; i++) {
        menu_row *row = &m->rows[i];
        if (row->page != m->page) {
            continue;
        }
        int chosen = strcmp(row->key, m->selected) == 0;
        float left = row->x0;
        float top = row->y0 + 11;
        if (chosen) {
            ui_text(win, left, top, L"\x25C6", 1, ui_font_get(L"Segoe UI", 11, 1), t->accent, UI_W, 0, 0);
        }
        uint32_t ink = chosen || m->hover == i ? t->text : t->text_muted;
        ui_text(win, left + 18, top, row->label, (int)wcslen(row->label), ui_font_get(L"Segoe UI", 11, chosen),
                ink, UI_W, 0, 0);
        ui_text(win, left + column_width - 24, top, row->note, (int)wcslen(row->note),
                ui_font_get(L"Segoe UI", 8, 0), chosen ? t->accent : t->text_muted, UI_E, 0, 0);
    }
    for (int p = 0; p < m->page_count; p++) {
        float cx, cy;
        dot_centre(m, p, &cx, &cy);
        ui_fill_ellipse(win, cx - MENU_DOT_RADIUS, cy - MENU_DOT_RADIUS, cx + MENU_DOT_RADIUS, cy + MENU_DOT_RADIUS,
                        p == m->page ? t->accent : t->panel_soft);
    }
}

static int menu_row_at(const ui_menu *m, float x, float y) {
    for (int i = 0; i < m->row_count; i++) {
        const menu_row *row = &m->rows[i];
        if (row->page == m->page && x >= row->x0 && x <= row->x1 && y >= row->y0 && y <= row->y1) {
            return i;
        }
    }
    return -1;
}

static int menu_dot_at(const ui_menu *m, float x, float y) {
    for (int p = 0; p < m->page_count; p++) {
        float cx, cy;
        dot_centre(m, p, &cx, &cy);
        float dx = x - cx;
        float dy = y - cy;
        if (dx * dx + dy * dy <= (MENU_DOT_RADIUS + 1) * (MENU_DOT_RADIUS + 1)) {
            return p;
        }
    }
    return -1;
}

int menu_move(ui_menu *m, float x, float y) {
    int row = menu_row_at(m, x, y);
    if (row != m->hover) {
        m->hover = row;
    }
    return row >= 0 || menu_dot_at(m, x, y) >= 0;
}

int menu_click(ui_menu *m, float x, float y) {
    int row = menu_row_at(m, x, y);
    if (row >= 0) {
        if (m->on_pick != NULL) {
            m->on_pick(m->ctx, m->rows[row].key);
        }
        return 1;
    }
    int dot = menu_dot_at(m, x, y);
    if (dot >= 0 && m->page_count > 0) {
        m->page = dot % m->page_count;
        m->hover = -1;
        menu_layout(m);
        return 1;
    }
    return 0;
}

static void star(ui_window *win, float cx, float cy, float radius, uint32_t rgb, float alpha, float pinch) {
    if (radius <= 0) {
        return;
    }
    float outer = radius / 2;
    float inner = outer * pinch;
    float points[16] = {
        cx, cy - outer, cx + inner, cy - inner, cx + outer, cy, cx + inner, cy + inner,
        cx, cy + outer, cx - inner, cy + inner, cx - outer, cy, cx - inner, cy - inner,
    };
    ui_fill_polygon(win, points, 8, rgb, alpha);
}

void sparkle_draw(ui_window *win, float cx, float cy, uint32_t rgb, int frame) {
    if (frame < 0 || frame >= SPARKLE_FRAMES) {
        return;
    }
    double progress = (double)frame / (SPARKLE_FRAMES - 1);
    float scale = (float)sin(progress * 3.14159265358979323846);
    if (scale <= 0.05f) {
        return;
    }
    star(win, cx, cy, SPARKLE_SIZE * scale, rgb, 40.0f / 255.0f, 0.2f);
    star(win, cx, cy, SPARKLE_SIZE * scale * 0.6f, rgb, 180.0f / 255.0f, 0.15f);
    star(win, cx, cy, SPARKLE_SIZE * scale * 0.25f, 0xFFFFFF, 1.0f, 0.1f);
}

static int rand_between(int low, int high) {
    return low + rand() % (high - low + 1);
}

int sparkle_cluster(ui_sparkle *out, int room, int spread) {
    int count = rand_between(3, 5);
    int made = 0;
    for (int i = 0; i < count; i++) {
        float x = (float)rand_between(-spread, spread);
        float y = (float)rand_between(-spread, spread);
        int frame = rand_between(-6, 0);
        if (made < room) {
            out[made].x = x;
            out[made].y = y;
            out[made].frame = frame;
            made++;
        }
    }
    return made;
}
