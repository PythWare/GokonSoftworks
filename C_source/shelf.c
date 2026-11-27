#include "gui.h"
#include "audio.h"
#include "bottleart.h"
#include "gsfs.h"
#include "ledger.h"
#include "recipe.h"
#include "returns.h"
#include "widgets.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define SHELF_WIDTH 1240
#define SHELF_HEIGHT 800
#define SHELF_DETAILS 560
#define SHELF_PAD 16
#define SHELF_TOP 62
#define SHELF_SIDE 380
#define SHELF_BUTTON_H 34
#define SHELF_BUTTON_GAP 8
#define PREVIEW_W (SHELF_SIDE - SHELF_PAD * 3)
#define PREVIEW_H 196
#define PREVIEW_Y (SHELF_PAD + 86)
#define NAV_Y (PREVIEW_Y + PREVIEW_H + 8)
#define CLASH_Y (NAV_Y + 32)
#define META_Y (NAV_Y + 54)
#define DESC_Y (META_Y + 108)
#define DESC_MIN 96
#define LOG_MIN 110
#define BOTTLE_H 230
#define BOTTLE_GAP 26
#define SHELF_MARGIN 34
#define SHELF_PLANK 18
#define SHELF_CAPTION 26
#define SHELF_OVERHANG 16
#define SHELF_BAR 8
#define SHELF_THUMB_MIN 24
#define SPARKLE_TICK 33
#define SPARKLE_SPAWN 1400
#define SPARKLE_MAX 7
#define TIMER_TICK 1
#define TIMER_SPAWN 2
#define ART_CACHE 64

enum { T_RESCAN, T_POUR, T_EMPTY, T_EMPTY_ALL, T_FOLDER, T_COUNT };
enum { S_PREV, S_NEXT, S_AUDIO, S_COUNT };
enum { JOB_POUR = 1, JOB_EMPTY, JOB_EMPTY_ALL };

static const uint32_t clash_colours[] = {0xE0913C, 0x4E9CC4, 0xB85A9C, 0x48B08A, 0xD24A6E, 0x8E5BC8};

typedef struct {
    gs_recipe recipe;
    int enabled;
    char problem[256];
    int has_clash;
    uint32_t clash;
    uint32_t colour;
    wchar_t *label;
    wchar_t *caption;
    ui_sparkle sparkles[SPARKLE_MAX];
    int sparkle_count;
    int visible;
} pour_item;

typedef struct {
    uint32_t colour;
    int filled;
    float height;
    ui_image *image;
} art_entry;

typedef struct {
    ui_window *win;
    gs_app *app;
    gs_game *game;
    char *game_dir;
    char *mods_dir;
    gs_tab *tab;
    int measured_now;
    pour_item *pours;
    int pour_count;
    int selected;
    bottle_base base;
    art_entry art[ART_CACHE];
    int art_count;
    float art_scale;
    float bottle_w;
    float label_width;
    float label_offset;
    int columns;
    int scroll_px;
    int shelf_dragging;
    float shelf_grab;
    ui_button top[T_COUNT];
    ui_button side[S_COUNT];
    ui_button *top_list[T_COUNT];
    ui_button *side_list[S_COUNT];
    ui_panel side_panel;
    ui_log desc;
    ui_log log;
    ui_progress progress;
    wchar_t *title;
    wchar_t *meta;
    wchar_t *clash_note;
    uint32_t clash_dot;
    int has_clash_dot;
    buf *previews;
    size_t preview_count;
    size_t preview_index;
    ui_image *preview_image;
    float preview_w;
    float preview_h;
    wchar_t *preview_text;
    int preview_text_shown;
    int preview_is_bottle;
    buf audio;
    int has_audio;
    audio_player player;
    int audio_enabled;
    int busy;
    int pressed_area;
} shelf_state;

typedef struct {
    gs_game *game;
    char *game_dir;
    char *recipe_path;
    char *mod_id;
    char *name;
    gs_tab *tab;
    int64_t a;
    int64_t b;
} shelf_work;

static float side_x(void) {
    return SHELF_WIDTH - SHELF_SIDE;
}

static float shelf_w(void) {
    return SHELF_WIDTH - SHELF_SIDE;
}

static float shelf_h(void) {
    return SHELF_HEIGHT - SHELF_TOP;
}

static void say(shelf_state *s, const char *text, ui_ink ink) {
    log_write(&s->log, text, ink);
    ui_invalidate(s->win);
}

static void set_wide(wchar_t **slot, const char *text) {
    free(*slot);
    *slot = text == NULL ? NULL : ui_wide(text);
}

static ui_image *art_for(shelf_state *s, uint32_t colour, int filled, float height) {
    if (s->art_scale != s->win->scale) {
        for (int i = 0; i < s->art_count; i++) {
            ui_image_free(s->art[i].image);
        }
        s->art_count = 0;
        s->art_scale = s->win->scale;
    }
    for (int i = 0; i < s->art_count; i++) {
        if (s->art[i].colour == colour && s->art[i].filled == filled && s->art[i].height == height) {
            return s->art[i].image;
        }
    }
    ui_image *image = bottle_render(&s->base, colour, filled, height, s->win->scale);
    if (s->art_count >= ART_CACHE) {
        ui_image_free(s->art[0].image);
        memmove(&s->art[0], &s->art[1], (ART_CACHE - 1) * sizeof(art_entry));
        s->art_count--;
    }
    s->art[s->art_count].colour = colour;
    s->art[s->art_count].filled = filled;
    s->art[s->art_count].height = height;
    s->art[s->art_count].image = image;
    s->art_count++;
    return image;
}

static void free_pours(shelf_state *s) {
    for (int i = 0; i < s->pour_count; i++) {
        recipe_free(&s->pours[i].recipe);
        free(s->pours[i].label);
        free(s->pours[i].caption);
    }
    free(s->pours);
    s->pours = NULL;
    s->pour_count = 0;
}

static int find_root(int *parent, int item) {
    while (parent[item] != item) {
        parent[item] = parent[parent[item]];
        item = parent[item];
    }
    return item;
}

typedef struct {
    int64_t marker;
    int64_t offset;
    int holder;
} slot_hold;

static int compare_hold(const void *a, const void *b) {
    const slot_hold *x = (const slot_hold *)a;
    const slot_hold *y = (const slot_hold *)b;
    if (x->marker != y->marker) {
        return x->marker < y->marker ? -1 : 1;
    }
    if (x->offset != y->offset) {
        return x->offset < y->offset ? -1 : 1;
    }
    return x->holder - y->holder;
}

static void find_clashes(shelf_state *s) {
    int n = s->pour_count;
    if (n == 0) {
        return;
    }
    int *parent = (int *)malloc((size_t)n * sizeof(int));
    int *clashed = (int *)calloc((size_t)n, sizeof(int));
    size_t total = 0;
    for (int i = 0; i < n; i++) {
        total += s->pours[i].recipe.entry_count;
    }
    slot_hold *holds = (slot_hold *)malloc((total + 1) * sizeof(slot_hold));
    if (parent == NULL || clashed == NULL || holds == NULL) {
        free(parent);
        free(clashed);
        free(holds);
        return;
    }
    size_t at = 0;
    for (int i = 0; i < n; i++) {
        parent[i] = i;
        for (size_t k = 0; k < s->pours[i].recipe.entry_count; k++) {
            holds[at].marker = s->pours[i].recipe.entries[k].idx_marker;
            holds[at].offset = s->pours[i].recipe.entries[k].entry_off;
            holds[at].holder = i;
            at++;
        }
    }
    qsort(holds, total, sizeof(slot_hold), compare_hold);
    for (size_t k = 0; k < total;) {
        size_t end = k;
        while (end < total && holds[end].marker == holds[k].marker && holds[end].offset == holds[k].offset) {
            end++;
        }
        int first = holds[k].holder;
        for (size_t q = k + 1; q < end; q++) {
            int other = holds[q].holder;
            if (other == first) {
                continue;
            }
            parent[find_root(parent, first)] = find_root(parent, other);
            clashed[first] = 1;
            clashed[other] = 1;
        }
        k = end;
    }
    int *numbering = (int *)malloc((size_t)n * sizeof(int));
    if (numbering != NULL) {
        for (int i = 0; i < n; i++) {
            numbering[i] = -1;
        }
        int next = 0;
        for (int i = 0; i < n; i++) {
            if (!clashed[i]) {
                continue;
            }
            int root = find_root(parent, i);
            if (numbering[root] < 0) {
                numbering[root] = next++;
            }
            s->pours[i].has_clash = 1;
            s->pours[i].clash = clash_colours[numbering[root] % 6];
        }
        free(numbering);
    }
    free(parent);
    free(clashed);
    free(holds);
}

static void label_for(shelf_state *s, pour_item *p) {
    ui_font *font = ui_font_get(L"Segoe UI", 7, 1);
    wchar_t *name = ui_wide(p->recipe.name);
    ui_lines lines;
    lines_init(&lines);
    wrap_lines(font, name, (int)wcslen(name), s->label_width, 3, &lines);
    p->label = join_lines(&lines);
    lines_free(&lines);
    free(name);
    char text[300];
    if (p->problem[0] != 0) {
        snprintf(text, sizeof(text), "%s", p->problem);
    } else {
        snprintf(text, sizeof(text), "%zu files", p->recipe.entry_count);
    }
    wchar_t *wide = ui_wide(text);
    p->caption = fit_line(font, wide, (int)wcslen(wide), s->bottle_w + BOTTLE_GAP - 4);
    free(wide);
}

static float cell_w(shelf_state *s) {
    return s->bottle_w + BOTTLE_GAP;
}

static float cell_h(void) {
    return BOTTLE_H + SHELF_PLANK + SHELF_CAPTION;
}

static int columns_for(shelf_state *s, float width) {
    float room = width - SHELF_MARGIN * 2 - SHELF_BAR;
    if (room < 1) {
        room = 1;
    }
    int columns = (int)(room / cell_w(s));
    return columns < 1 ? 1 : columns;
}

static int visible_rows(void) {
    return (int)(shelf_h() / cell_h()) + 2;
}

static int row_count(shelf_state *s) {
    if (s->pour_count == 0) {
        return 0;
    }
    int columns = s->columns < 1 ? 1 : s->columns;
    return (s->pour_count + columns - 1) / columns;
}

static float content_height(shelf_state *s) {
    return SHELF_MARGIN * 2 + row_count(s) * cell_h();
}

static int max_scroll(shelf_state *s) {
    float value = content_height(s) - shelf_h();
    return value > 0 ? (int)value : 0;
}

static void clamp_scroll(shelf_state *s) {
    int limit = max_scroll(s);
    if (s->scroll_px > limit) {
        s->scroll_px = limit;
    }
    if (s->scroll_px < 0) {
        s->scroll_px = 0;
    }
}

static int first_row(shelf_state *s) {
    int row = (int)floor((s->scroll_px - SHELF_MARGIN) / cell_h());
    return row < 0 ? 0 : row;
}

static void track_bounds(float *l, float *t, float *r, float *b) {
    float left = shelf_w() - SHELF_BAR - 4;
    *l = left;
    *t = SHELF_TOP + 4;
    *r = left + SHELF_BAR;
    *b = SHELF_TOP + shelf_h() - 4;
}

static int thumb_bounds(shelf_state *s, float *l, float *t, float *r, float *b) {
    int limit = max_scroll(s);
    if (limit <= 0) {
        return 0;
    }
    float left, top, right, bottom;
    track_bounds(&left, &top, &right, &bottom);
    float span = bottom - top;
    float height = span * (shelf_h() / content_height(s));
    if (height < SHELF_THUMB_MIN) {
        height = SHELF_THUMB_MIN;
    }
    float travel = span - height;
    float start = top + travel * ((float)s->scroll_px / limit);
    *l = left;
    *t = start;
    *r = right;
    *b = start + height;
    return 1;
}

static void update_visibility(shelf_state *s) {
    int first = first_row(s);
    int rows = visible_rows();
    int columns = s->columns < 1 ? 1 : s->columns;
    for (int i = 0; i < s->pour_count; i++) {
        int row = i / columns;
        int visible = row >= first && row < first + rows;
        if (!visible && s->pours[i].visible) {
            s->pours[i].sparkle_count = 0;
        }
        s->pours[i].visible = visible;
    }
}

static void update_buttons(shelf_state *s) {
    pour_item *p = s->selected >= 0 ? &s->pours[s->selected] : NULL;
    int ready = !s->busy;
    size_t enabled_count = 0;
    char **ids = s->tab != NULL ? tab_enabled_ids(s->tab, &enabled_count) : NULL;
    free(ids);
    s->top[T_POUR].enabled = p != NULL && ready && !p->enabled;
    s->top[T_EMPTY].enabled = p != NULL && ready && p->enabled;
    s->top[T_EMPTY_ALL].enabled = ready && enabled_count > 0;
    s->top[T_RESCAN].enabled = ready;
    s->top[T_FOLDER].enabled = 1;
    ui_invalidate(s->win);
}

static void stop_audio(shelf_state *s) {
    audio_stop(&s->player);
}

static void refresh_audio(shelf_state *s) {
    if (!s->audio_enabled || !s->has_audio || s->audio.len == 0) {
        stop_audio(s);
        return;
    }
    if (!s->player.available) {
        say(s, "Audio playback is unavailable on this system.", INK_DANGER);
        return;
    }
    if (!audio_play_loop(&s->player, s->audio.data, s->audio.len)) {
        say(s, "Bundled audio isnt a playable WAV.", INK_DANGER);
    }
}

static void free_previews(shelf_state *s) {
    for (size_t i = 0; i < s->preview_count; i++) {
        buf_free(&s->previews[i]);
    }
    free(s->previews);
    s->previews = NULL;
    s->preview_count = 0;
    s->preview_index = 0;
}

static void set_preview_text(shelf_state *s, const char *text, int shown) {
    set_wide(&s->preview_text, text);
    s->preview_text_shown = shown;
}

static void show_preview(shelf_state *s) {
    ui_image_free(s->preview_image);
    s->preview_image = NULL;
    s->preview_is_bottle = 0;
    pour_item *p = s->selected >= 0 ? &s->pours[s->selected] : NULL;
    if (s->preview_count == 0) {
        if (p != NULL) {
            s->preview_is_bottle = 1;
            s->preview_w = bottle_width_for(&s->base, PREVIEW_H - 16);
            s->preview_h = PREVIEW_H - 16;
            s->preview_text_shown = 0;
        }
        return;
    }
    s->preview_index %= s->preview_count;
    char why[200] = "";
    const buf *blob = &s->previews[s->preview_index];
    ui_image *decoded = ui_image_decode(blob->data, blob->len, why, sizeof(why));
    if (decoded == NULL) {
        char text[300];
        snprintf(text, sizeof(text), "Preview error: %s", why);
        set_preview_text(s, text, 1);
        return;
    }
    s->preview_image = ui_image_scaled(decoded, ui_px(s->win, PREVIEW_W), ui_px(s->win, PREVIEW_H));
    ui_image_free(decoded);
    s->preview_w = PREVIEW_W;
    s->preview_h = PREVIEW_H;
    s->preview_text_shown = 0;
}

static void clash_text(shelf_state *s, pour_item *p) {
    int others = 0;
    for (int i = 0; i < s->pour_count; i++) {
        others += &s->pours[i] != p && s->pours[i].has_clash && s->pours[i].clash == p->clash;
    }
    if (others == 0) {
        set_wide(&s->clash_note, NULL);
        return;
    }
    const char **names = (const char **)calloc((size_t)others, sizeof(char *));
    int n = 0;
    for (int i = 0; names != NULL && i < s->pour_count; i++) {
        if (&s->pours[i] != p && s->pours[i].has_clash && s->pours[i].clash == p->clash) {
            names[n++] = s->pours[i].recipe.name;
        }
    }
    ui_font *font = ui_font_get(L"Segoe UI", 9, 0);
    float width = SHELF_SIDE - SHELF_PAD * 2 - 22;
    char candidate[1024] = "";
    int start = n < 2 ? n : 2;
    for (int shown = start; shown >= 0; shown--) {
        char named[800] = "";
        for (int k = 0; k < shown; k++) {
            size_t len = strlen(named);
            snprintf(named + len, sizeof(named) - len, "%s%s", k > 0 ? ", " : "", names[k]);
        }
        int extra = n - shown;
        char body[900];
        if (named[0] != 0 && extra) {
            snprintf(body, sizeof(body), "%s and %d more", named, extra);
        } else if (named[0] != 0) {
            snprintf(body, sizeof(body), "%s", named);
        } else {
            snprintf(body, sizeof(body), "%d other bottles", extra);
        }
        snprintf(candidate, sizeof(candidate), "Shares slots with %s", body);
        wchar_t *wide = ui_wide(candidate);
        float measured = ui_measure(font, wide, (int)wcslen(wide));
        free(wide);
        if (measured <= width) {
            set_wide(&s->clash_note, candidate);
            free(names);
            return;
        }
    }
    wchar_t *wide = ui_wide(candidate);
    free(s->clash_note);
    s->clash_note = fit_line(font, wide, (int)wcslen(wide), width);
    free(wide);
    free(names);
}

static void select_pour(shelf_state *s, int index) {
    s->selected = index;
    log_clear(&s->desc);
    if (index < 0) {
        set_wide(&s->title, "Nothing selected");
        set_wide(&s->meta, NULL);
        free_previews(s);
        buf_reset(&s->audio);
        s->has_audio = 0;
        stop_audio(s);
        ui_image_free(s->preview_image);
        s->preview_image = NULL;
        s->preview_is_bottle = 0;
        set_preview_text(s, "No bottle picked", 1);
        s->has_clash_dot = 0;
        set_wide(&s->clash_note, NULL);
        update_buttons(s);
        return;
    }
    pour_item *p = &s->pours[index];
    gs_recipe *r = &p->recipe;
    set_wide(&s->title, r->name);
    free_previews(s);
    if (r->image_count > 0) {
        s->previews = (buf *)calloc(r->image_count, sizeof(buf));
        if (s->previews != NULL) {
            for (size_t i = 0; i < r->image_count; i++) {
                buf_init(&s->previews[i]);
            }
            err e;
            err_clear(&e);
            if (recipe_read_images(r, s->previews, &e)) {
                s->preview_count = r->image_count;
            } else {
                for (size_t i = 0; i < r->image_count; i++) {
                    buf_free(&s->previews[i]);
                }
                free(s->previews);
                s->previews = NULL;
                char text[700];
                snprintf(text, sizeof(text), "couldnt read previews: %s", e.text);
                say(s, text, INK_DANGER);
            }
        }
    }
    s->preview_index = 0;
    show_preview(s);
    buf_reset(&s->audio);
    s->has_audio = 0;
    err e;
    err_clear(&e);
    int present = 0;
    if (recipe_read_audio(r, &s->audio, &present, &e) && present) {
        s->has_audio = 1;
    }
    refresh_audio(s);
    char meta[600] = "";
    char size[48];
    gs_human_size(r->payload_bytes, size, sizeof(size));
    size_t at = 0;
    at += (size_t)snprintf(meta + at, sizeof(meta) - at, "%s", p->enabled ? "Poured" : "Sealed");
    if (r->author[0] != 0) {
        at += (size_t)snprintf(meta + at, sizeof(meta) - at, "\nby %s", r->author);
    }
    if (r->version_text[0] != 0) {
        at += (size_t)snprintf(meta + at, sizeof(meta) - at, "\nv%s", r->version_text);
    }
    at += (size_t)snprintf(meta + at, sizeof(meta) - at, "\n%zu files\n%s", r->entry_count, size);
    set_wide(&s->meta, meta);
    log_set_text(&s->desc, r->description[0] != 0 ? r->description : "No notes on this bottle.", INK_MUTED);
    if (p->has_clash) {
        s->has_clash_dot = 1;
        s->clash_dot = p->clash;
        clash_text(s, p);
    } else {
        s->has_clash_dot = 0;
        set_wide(&s->clash_note, NULL);
    }
    update_buttons(s);
}

static int64_t dead_bytes(shelf_state *s) {
    int64_t total = 0;
    for (int i = 0; i < s->game->container_count; i++) {
        int64_t original = 0;
        if (!tab_original_size(s->tab, i, &original)) {
            continue;
        }
        char *path = returns_container_file(s->game, s->game_dir, i, s->game->containers[i]);
        if (path != NULL && path_is_file(path)) {
            int64_t size = path_file_size(path);
            total += size > original ? size - original : 0;
        }
        free(path);
    }
    return total;
}

static void rescan(shelf_state *s) {
    if (s->busy) {
        return;
    }
    char keep[300] = "";
    if (s->selected >= 0) {
        snprintf(keep, sizeof(keep), "%s", s->pours[s->selected].recipe.mod_id);
    }
    stop_audio(s);
    s->selected = -1;
    free_pours(s);
    recipe_scan scan;
    recipe_scan_folder(s->mods_dir, s->game->game_id, s->game->game_index, &scan);
    s->pours = (pour_item *)calloc(scan.count + 1, sizeof(pour_item));
    if (s->pours != NULL) {
        for (size_t i = 0; i < scan.count; i++) {
            pour_item *p = &s->pours[i];
            p->recipe = scan.items[i];
            p->enabled = s->tab != NULL && tab_is_enabled(s->tab, p->recipe.mod_id);
            p->colour = bottle_colour(p->recipe.colour);
        }
        s->pour_count = (int)scan.count;
        free(scan.items);
        scan.items = NULL;
        scan.count = 0;
    }
    find_clashes(s);
    for (int i = 0; i < s->pour_count; i++) {
        label_for(s, &s->pours[i]);
    }
    for (size_t i = 0; i < scan.problem_count; i++) {
        char text[900];
        snprintf(text, sizeof(text), "%s: %s", gs_path_name(scan.problem_paths[i]), scan.problem_texts[i]);
        say(s, text, INK_DANGER);
    }
    recipe_scan_free(&scan);
    s->columns = columns_for(s, shelf_w());
    clamp_scroll(s);
    update_visibility(s);
    int poured = 0;
    for (int i = 0; i < s->pour_count; i++) {
        poured += s->pours[i].enabled;
    }
    if (s->measured_now) {
        buf text;
        buf_init(&text);
        buf_puts(&text, "Measured vanilla containers: ");
        int first = 1;
        for (int i = 0; i < s->game->container_count; i++) {
            int64_t size = 0;
            if (tab_original_size(s->tab, i, &size)) {
                char human[48];
                gs_human_size(size, human, sizeof(human));
                buf_printf(&text, "%s%s %s", first ? "" : ", ", s->game->containers[i], human);
                first = 0;
            }
        }
        say(s, text.data, INK_MUTED);
        buf_free(&text);
        s->measured_now = 0;
    }
    char line[600];
    snprintf(line, sizeof(line), "%d bottles on the shelf, %d poured.", s->pour_count, poured);
    say(s, line, INK_ACCENT);
    int64_t dead = s->tab != NULL ? dead_bytes(s) : 0;
    if (dead && !poured) {
        char human[48];
        gs_human_size(dead, human, sizeof(human));
        snprintf(line, sizeof(line),
                 "%s of appended data is still on the containers from earlier mods. Empty Every Bottle slices it off.",
                 human);
        say(s, line, INK_MUTED);
    }
    if (s->pour_count == 0) {
        snprintf(line, sizeof(line), "Build a mod in the hub's Mod Creator or drop .gokon files into %s.", s->mods_dir);
        say(s, line, INK_MUTED);
    }
    int found = -1;
    for (int i = 0; keep[0] != 0 && i < s->pour_count; i++) {
        if (strcmp(s->pours[i].recipe.mod_id, keep) == 0) {
            found = i;
        }
    }
    select_pour(s, found);
    update_buttons(s);
}

static void cycle_preview(shelf_state *s, int delta) {
    if (s->preview_count == 0) {
        return;
    }
    s->preview_index = (s->preview_index + s->preview_count + (size_t)(delta < 0 ? s->preview_count - 1 : 1)) %
                       s->preview_count;
    show_preview(s);
    ui_invalidate(s->win);
}

static void on_prev(void *ctx) {
    cycle_preview((shelf_state *)ctx, -1);
}

static void on_next(void *ctx) {
    cycle_preview((shelf_state *)ctx, 1);
}

static void on_audio(void *ctx) {
    shelf_state *s = (shelf_state *)ctx;
    s->audio_enabled = !s->audio_enabled;
    button_set_text(&s->side[S_AUDIO], s->audio_enabled ? "Music: On" : "Music: Off");
    settings_set_bool(&s->app->settings, "mod_audio", s->audio_enabled);
    settings_save(&s->app->settings);
    refresh_audio(s);
    say(s, s->audio_enabled ? "Theme tunes on." : "Theme tunes off.", INK_MUTED);
}

static void release_work(ui_job *job) {
    shelf_work *work = (shelf_work *)job->data;
    if (work == NULL) {
        return;
    }
    gamedesc_free(work->game);
    free(work->game_dir);
    free(work->recipe_path);
    free(work->mod_id);
    free(work->name);
    if (work->tab != NULL) {
        tab_free(work->tab);
        free(work->tab);
    }
    free(work);
    job->data = NULL;
}

static void job_progress_cb(void *ctx, int64_t done, int64_t total) {
    ui_job_report((ui_job *)ctx, done, total);
}

static int run_pour(ui_job *job) {
    shelf_work *work = (shelf_work *)job->data;
    gs_recipe r;
    if (!recipe_read(work->recipe_path, &r, &job->e)) {
        recipe_free(&r);
        return 0;
    }
    int64_t poured = 0;
    int touched = 0;
    int ok = returns_apply(&r, work->game, work->game_dir, work->tab, job_progress_cb, job, &poured, &touched, &job->e);
    work->a = poured;
    recipe_free(&r);
    return ok;
}

static int run_empty(ui_job *job) {
    shelf_work *work = (shelf_work *)job->data;
    return returns_disable(work->mod_id, work->game, work->game_dir, work->tab, &work->a, &job->e);
}

static int run_empty_all(ui_job *job) {
    shelf_work *work = (shelf_work *)job->data;
    return returns_disable_all(work->game, work->game_dir, work->tab, &work->a, &work->b, &job->e);
}

static void run_job(shelf_state *s, int kind, ui_job_fn run, const char *label, const char *mod_id,
                    const char *recipe_path, const char *name) {
    if (s->busy || s->tab == NULL) {
        return;
    }
    shelf_work *work = (shelf_work *)calloc(1, sizeof(shelf_work));
    if (work == NULL) {
        return;
    }
    work->game = gamedesc_load(s->game->game_id);
    work->game_dir = _strdup(s->game_dir);
    work->mod_id = mod_id != NULL ? _strdup(mod_id) : NULL;
    work->recipe_path = recipe_path != NULL ? _strdup(recipe_path) : NULL;
    work->name = name != NULL ? _strdup(name) : NULL;
    work->tab = s->tab;
    ui_job *job = ui_job_new(s->win->hwnd, kind, 0, run, release_work, work);
    if (job == NULL || work->game == NULL) {
        work->tab = NULL;
        if (job != NULL) {
            ui_job_free(job);
        } else {
            gamedesc_free(work->game);
            free(work->game_dir);
            free(work->mod_id);
            free(work->recipe_path);
            free(work->name);
            free(work);
        }
        return;
    }
    s->tab = NULL;
    s->busy = 1;
    update_buttons(s);
    progress_set(&s->progress, 0.0f);
    say(s, label, INK_ACCENT);
    if (!ui_job_start(job)) {
        s->tab = work->tab;
        work->tab = NULL;
        ui_job_free(job);
        s->busy = 0;
        update_buttons(s);
    }
}

static void enable_selected(void *ctx) {
    shelf_state *s = (shelf_state *)ctx;
    if (s->selected < 0) {
        ui_message(s->win->hwnd, "Pour", "Pick a bottle off the shelf first.", MB_ICONINFORMATION);
        ui_keep_front(s->win->hwnd);
        return;
    }
    pour_item *p = &s->pours[s->selected];
    char text[600];
    if (p->enabled) {
        snprintf(text, sizeof(text), "%s is already poured.", p->recipe.name);
        say(s, text, INK_DANGER);
        return;
    }
    if (p->problem[0] != 0) {
        ui_message(s->win->hwnd, "Pour", p->problem, MB_ICONERROR);
        return;
    }
    if (p->recipe.entry_count == 0) {
        ui_message(s->win->hwnd, "Pour", "This package has no files in it.", MB_ICONERROR);
        return;
    }
    snprintf(text, sizeof(text), "Pouring %s", p->recipe.name);
    run_job(s, JOB_POUR, run_pour, text, p->recipe.mod_id, p->recipe.path, p->recipe.name);
}

static void disable_selected(void *ctx) {
    shelf_state *s = (shelf_state *)ctx;
    if (s->selected < 0) {
        ui_message(s->win->hwnd, "Empty", "Pick a bottle off the shelf first.", MB_ICONINFORMATION);
        ui_keep_front(s->win->hwnd);
        return;
    }
    pour_item *p = &s->pours[s->selected];
    char text[600];
    if (!p->enabled) {
        snprintf(text, sizeof(text), "%s is already empty.", p->recipe.name);
        say(s, text, INK_DANGER);
        return;
    }
    snprintf(text, sizeof(text), "Emptying %s", p->recipe.name);
    run_job(s, JOB_EMPTY, run_empty, text, p->recipe.mod_id, NULL, p->recipe.name);
}

static void disable_all(void *ctx) {
    shelf_state *s = (shelf_state *)ctx;
    int poured = 0;
    for (int i = 0; i < s->pour_count; i++) {
        poured += s->pours[i].enabled;
    }
    size_t enabled_count = 0;
    char **ids = s->tab != NULL ? tab_enabled_ids(s->tab, &enabled_count) : NULL;
    free(ids);
    if (!poured && enabled_count == 0) {
        say(s, "Every bottle is already empty and the containers are vanilla sized.", INK_MUTED);
        return;
    }
    char text[600];
    if (poured) {
        snprintf(text, sizeof(text),
                 "Put every container index back to vanilla and cut every appended byte off the end?\n\n"
                 "This empties %d poured bottles.",
                 poured);
    } else {
        snprintf(text, sizeof(text),
                 "Put every container index back to vanilla and cut every appended byte off the end?");
    }
    if (!ui_ask_yes_no(s->win->hwnd, "Empty Every Bottle", text)) {
        return;
    }
    run_job(s, JOB_EMPTY_ALL, run_empty_all, "Emptying the whole shelf", NULL, NULL, NULL);
}

static void on_rescan(void *ctx) {
    rescan((shelf_state *)ctx);
    ui_invalidate(((shelf_state *)ctx)->win);
}

static void open_mods_folder(void *ctx) {
    shelf_state *s = (shelf_state *)ctx;
    path_make_dirs(s->mods_dir);
    ui_open_explorer(s->mods_dir);
}

static void shelf_progress(ui_window *w, ui_job *job, int64_t done, int64_t total) {
    shelf_state *s = (shelf_state *)w->self;
    (void)job;
    if (progress_set(&s->progress, total ? (float)((double)done / (double)total) : 0.0f)) {
        ui_invalidate(w);
    }
}

static void shelf_done(ui_window *w, ui_job *job) {
    shelf_state *s = (shelf_state *)w->self;
    shelf_work *work = (shelf_work *)job->data;
    s->tab = work->tab;
    work->tab = NULL;
    s->busy = 0;
    if (!job->ok) {
        progress_set(&s->progress, 0.0f);
        char text[ERR_MAX + 32];
        snprintf(text, sizeof(text), "Error: %s", job->e.text);
        say(s, text, INK_DANGER);
        gs_log("ERROR", "Mod Shelf job failed: %s", job->e.text);
        update_buttons(s);
        snprintf(text, sizeof(text), "Error\n\n%s", job->e.text);
        ui_message(w->hwnd, "Mod Shelf", text, MB_ICONERROR);
        return;
    }
    progress_set(&s->progress, 1.0f);
    char text[700];
    if (job->kind == JOB_EMPTY) {
        snprintf(text, sizeof(text),
                 "Emptied %s, %lld TOC entr%s back to vanilla. Empty Every Bottle is what trims the containers.",
                 work->name == NULL ? "" : work->name, (long long)work->a, work->a == 1 ? "y" : "ies");
        say(s, text, INK_OK);
    } else if (job->kind == JOB_EMPTY_ALL) {
        snprintf(text, sizeof(text), "Emptied the shelf: %lld TOC entr%s back to vanilla, %lld containers trimmed.",
                 (long long)work->a, work->a == 1 ? "y" : "ies", (long long)work->b);
        say(s, text, INK_OK);
    } else {
        snprintf(text, sizeof(text), "Pouring %s done.", work->name == NULL ? "" : work->name);
        say(s, text, INK_OK);
    }
    if (s->tab != NULL) {
        tab_free(s->tab);
        free(s->tab);
        s->tab = NULL;
    }
    gs_tab *tab = (gs_tab *)calloc(1, sizeof(gs_tab));
    if (tab != NULL && tab_open(tab, NULL, s->game_dir, s->game->game_id)) {
        s->tab = tab;
    } else {
        free(tab);
    }
    rescan(s);
    ui_keep_front(w->hwnd);
}

static void draw_bottle(ui_window *w, shelf_state *s, pour_item *p, float x, float y, int selected) {
    const gs_theme *t = w->theme;
    float width = s->bottle_w;
    float height = BOTTLE_H;
    float cx = x + width / 2;
    float cy = y + height / 2;
    if (selected) {
        ui_stroke_rect(w, x - 6, y - 6, x + width + 7, y + height + 7, t->accent, 2.0f);
    }
    ui_image *art = art_for(s, p->colour, p->enabled, BOTTLE_H);
    ui_draw_image(w, art, x, y, x + width, y + height);
    if (p->label != NULL) {
        ui_text(w, cx, cy + s->label_offset, p->label, (int)wcslen(p->label), ui_font_get(L"Segoe UI", 7, 1),
                0x3b3226, UI_CENTER, s->label_width, 1);
    }
    if (p->caption != NULL) {
        ui_text(w, cx, y + height + SHELF_PLANK + 6, p->caption, (int)wcslen(p->caption),
                ui_font_get(L"Segoe UI", 7, 0), p->problem[0] ? t->danger : t->text_muted, UI_N,
                width + BOTTLE_GAP, 1);
    }
    if (p->has_clash) {
        float radius = 7;
        ui_fill_ellipse(w, x + width - radius * 2, y, x + width, y + radius * 2, p->clash);
    }
    for (int i = 0; i < p->sparkle_count; i++) {
        if (p->sparkles[i].frame >= 0) {
            sparkle_draw(w, cx + p->sparkles[i].x, cy + p->sparkles[i].y, p->colour, p->sparkles[i].frame);
        }
    }
}

static void paint_shelf(ui_window *w, shelf_state *s) {
    const gs_theme *t = w->theme;
    ui_push_clip(w, 0, SHELF_TOP, shelf_w(), SHELF_HEIGHT);
    float cw = cell_w(s);
    float ch = cell_h();
    int first = first_row(s);
    int rows = row_count(s);
    float plank_left = SHELF_MARGIN - SHELF_OVERHANG;
    float plank_right = SHELF_MARGIN + s->columns * cw - BOTTLE_GAP + SHELF_OVERHANG;
    for (int offset = 0; offset < visible_rows(); offset++) {
        int row = first + offset;
        if (row >= rows) {
            break;
        }
        float plank_y = SHELF_TOP + SHELF_MARGIN + row * ch + BOTTLE_H - s->scroll_px;
        ui_fill_rect(w, plank_left, plank_y, plank_right, plank_y + SHELF_PLANK, t->shelf);
        ui_fill_rect(w, plank_left, plank_y, plank_right, plank_y + 4, t->shelf_edge);
        ui_line(w, plank_left, plank_y + SHELF_PLANK, plank_right, plank_y + SHELF_PLANK, t->panel_soft, 1.0f);
    }
    for (int i = 0; i < s->pour_count; i++) {
        if (!s->pours[i].visible) {
            continue;
        }
        int row = i / s->columns;
        int column = i % s->columns;
        draw_bottle(w, s, &s->pours[i], SHELF_MARGIN + column * cw,
                    SHELF_TOP + SHELF_MARGIN + row * ch - s->scroll_px, i == s->selected);
    }
    float l, tp, r, b;
    if (thumb_bounds(s, &l, &tp, &r, &b)) {
        float tl, tt, tr, tb;
        track_bounds(&tl, &tt, &tr, &tb);
        ui_fill_rect(w, tl, tt, tr, tb, t->panel);
        ui_fill_rect(w, l, tp, r, b, s->shelf_dragging ? t->accent : t->text_muted);
    }
    ui_pop_clip(w);
}

static void paint_side(ui_window *w, shelf_state *s) {
    const gs_theme *t = w->theme;
    float ox = side_x();
    float oy = SHELF_TOP;
    panel_draw(w, &s->side_panel);
    if (s->title != NULL) {
        ui_text(w, ox + SHELF_PAD, oy + SHELF_PAD + 40, s->title, (int)wcslen(s->title),
                ui_font_get(L"Segoe UI", 12, 1), t->text, UI_NW, SHELF_SIDE - SHELF_PAD * 2, 0);
    }
    float px0 = ox + SHELF_PAD;
    float py0 = oy + PREVIEW_Y;
    ui_fill_rect(w, px0, py0, px0 + PREVIEW_W, py0 + PREVIEW_H, t->field);
    ui_stroke_rect(w, px0, py0, px0 + PREVIEW_W + 1, py0 + PREVIEW_H + 1, t->panel_soft, 1.0f);
    float pcx = px0 + PREVIEW_W / 2.0f;
    float pcy = py0 + PREVIEW_H / 2.0f;
    if (s->preview_is_bottle && s->selected >= 0) {
        pour_item *p = &s->pours[s->selected];
        ui_image *art = art_for(s, p->colour, p->enabled, PREVIEW_H - 16);
        ui_draw_image(w, art, pcx - s->preview_w / 2, pcy - s->preview_h / 2, pcx + s->preview_w / 2,
                      pcy + s->preview_h / 2);
    } else if (s->preview_image != NULL) {
        ui_draw_image(w, s->preview_image, pcx - s->preview_w / 2, pcy - s->preview_h / 2, pcx + s->preview_w / 2,
                      pcy + s->preview_h / 2);
    }
    if (s->preview_text_shown && s->preview_text != NULL) {
        ui_text(w, pcx, pcy, s->preview_text, (int)wcslen(s->preview_text), ui_font_get(L"Segoe UI", 9, 0),
                t->text_muted, UI_CENTER, 0, 1);
    }
    char count[48];
    if (s->preview_count > 0) {
        snprintf(count, sizeof(count), "%zu/%zu", s->preview_index + 1, s->preview_count);
    } else {
        snprintf(count, sizeof(count), "0/0");
    }
    ui_text_utf8(w, ox + SHELF_PAD + 78, oy + NAV_Y + 13, count, ui_font_get(L"Segoe UI", 9, 0), t->text_muted, UI_W,
                 0, 0);
    for (int i = 0; i < S_COUNT; i++) {
        button_draw(w, &s->side[i]);
    }
    if (s->has_clash_dot) {
        ui_fill_ellipse(w, ox + SHELF_PAD, oy + CLASH_Y + 2, ox + SHELF_PAD + 14, oy + CLASH_Y + 16, s->clash_dot);
    }
    if (s->clash_note != NULL) {
        ui_text(w, ox + SHELF_PAD + 22, oy + CLASH_Y, s->clash_note, (int)wcslen(s->clash_note),
                ui_font_get(L"Segoe UI", 9, 0), t->text_muted, UI_NW, SHELF_SIDE - SHELF_PAD * 2 - 22, 0);
    }
    if (s->meta != NULL) {
        ui_text(w, ox + SHELF_PAD, oy + META_Y, s->meta, (int)wcslen(s->meta), ui_font_get(L"Segoe UI", 9, 0),
                t->text_muted, UI_NW, SHELF_SIDE - SHELF_PAD * 2, 0);
    }
    log_draw(w, &s->desc);
    progress_draw(w, t, &s->progress);
    log_draw(w, &s->log);
}

static void shelf_paint(ui_window *w) {
    shelf_state *s = (shelf_state *)w->self;
    for (int i = 0; i < T_COUNT; i++) {
        button_draw(w, &s->top[i]);
    }
    paint_shelf(w, s);
    paint_side(w, s);
}

static int in_shelf(float x, float y) {
    return x < shelf_w() && y >= SHELF_TOP;
}

static int bottle_at(shelf_state *s, float x, float y) {
    float cw = cell_w(s);
    float ch = cell_h();
    for (int i = 0; i < s->pour_count; i++) {
        if (!s->pours[i].visible) {
            continue;
        }
        int row = i / s->columns;
        int column = i % s->columns;
        float bx = SHELF_MARGIN + column * cw;
        float by = SHELF_TOP + SHELF_MARGIN + row * ch - s->scroll_px;
        if (x >= bx && x <= bx + s->bottle_w && y >= by && y <= by + BOTTLE_H) {
            return i;
        }
        if (i == s->selected && x >= bx - 7 && x <= bx + s->bottle_w + 7 && y >= by - 7 && y <= by + BOTTLE_H + 7 &&
            (x <= bx - 4 || x >= bx + s->bottle_w + 4 || y <= by - 4 || y >= by + BOTTLE_H + 4)) {
            return i;
        }
    }
    return -1;
}

static void scroll_to(shelf_state *s, int px) {
    int limit = max_scroll(s);
    int target = px < 0 ? 0 : (px > limit ? limit : px);
    if (target == s->scroll_px) {
        return;
    }
    s->scroll_px = target;
    update_visibility(s);
    ui_invalidate(s->win);
}

static void shelf_drag_to(shelf_state *s, float py) {
    float l, t, r, b;
    if (!thumb_bounds(s, &l, &t, &r, &b)) {
        return;
    }
    float tl, tt, tr, tb;
    track_bounds(&tl, &tt, &tr, &tb);
    float travel = (tb - tt) - (b - t);
    if (travel <= 0) {
        return;
    }
    double fraction = (py - s->shelf_grab - tt) / travel;
    fraction = fraction < 0 ? 0 : (fraction > 1 ? 1 : fraction);
    scroll_to(s, (int)nearbyint(max_scroll(s) * fraction));
}

static void shelf_move(ui_window *w, float x, float y) {
    shelf_state *s = (shelf_state *)w->self;
    int changed = buttons_move(s->top_list, T_COUNT, x, y);
    changed |= buttons_move(s->side_list, S_COUNT, x, y);
    int hand = 0;
    for (int i = 0; i < T_COUNT; i++) {
        hand |= s->top[i].hovering;
    }
    for (int i = 0; i < S_COUNT; i++) {
        hand |= s->side[i].hovering;
    }
    w->hand_cursor = hand;
    if (s->shelf_dragging && (GetKeyState(VK_LBUTTON) & 0x8000)) {
        shelf_drag_to(s, y);
        changed = 1;
    }
    if (s->desc.dragging) {
        log_drag_to(&s->desc, y);
        changed = 1;
    } else if (s->log.dragging) {
        log_drag_to(&s->log, y);
        changed = 1;
    }
    if (changed) {
        ui_invalidate(w);
    }
}

static void shelf_down(ui_window *w, float x, float y) {
    shelf_state *s = (shelf_state *)w->self;
    buttons_down(s->top_list, T_COUNT, x, y);
    buttons_down(s->side_list, S_COUNT, x, y);
    if (in_shelf(x, y)) {
        float l, t, r, b;
        if (thumb_bounds(s, &l, &t, &r, &b)) {
            float tl, tt, tr, tb;
            track_bounds(&tl, &tt, &tr, &tb);
            if (l - 4 <= x && x <= r + 4 && t <= y && y <= b) {
                s->shelf_dragging = 1;
                s->shelf_grab = y - t;
                ui_invalidate(w);
                return;
            }
            if (tl - 4 <= x && x <= tr + 4 && tt <= y && y <= tb) {
                s->shelf_dragging = 1;
                s->shelf_grab = (b - t) / 2;
                shelf_drag_to(s, y);
                ui_invalidate(w);
                return;
            }
        }
        int hit = bottle_at(s, x, y);
        select_pour(s, hit);
        ui_invalidate(w);
        return;
    }
    if (x >= side_x() && y >= SHELF_TOP) {
        if (log_begin_drag(&s->desc, x, y) || log_begin_drag(&s->log, x, y)) {
            ui_invalidate(w);
        }
    }
}

static void shelf_up(ui_window *w, float x, float y) {
    shelf_state *s = (shelf_state *)w->self;
    if (s->shelf_dragging) {
        s->shelf_dragging = 0;
    }
    if (s->desc.dragging) {
        log_end_drag(&s->desc);
    }
    if (s->log.dragging) {
        log_end_drag(&s->log);
    }
    buttons_up(s->top_list, T_COUNT, x, y);
    buttons_up(s->side_list, S_COUNT, x, y);
    ui_invalidate(w);
}

static void shelf_leave(ui_window *w) {
    shelf_state *s = (shelf_state *)w->self;
    buttons_leave(s->top_list, T_COUNT);
    buttons_leave(s->side_list, S_COUNT);
    w->hand_cursor = 0;
    ui_invalidate(w);
}

static void shelf_wheel(ui_window *w, float x, float y, int delta) {
    shelf_state *s = (shelf_state *)w->self;
    if (in_shelf(x, y)) {
        scroll_to(s, s->scroll_px + (delta > 0 ? -1 : 1) * (int)(cell_h() / 3));
        return;
    }
    if (x >= side_x() && y >= SHELF_TOP) {
        if (log_wheel(&s->desc, x, y, delta) || log_wheel(&s->log, x, y, delta)) {
            ui_invalidate(w);
        }
    }
}

static void spawn_sparkles(shelf_state *s) {
    for (int i = 0; i < s->pour_count; i++) {
        pour_item *p = &s->pours[i];
        if (!p->visible || !p->enabled) {
            continue;
        }
        int room = SPARKLE_MAX - p->sparkle_count;
        if (room <= 0) {
            continue;
        }
        p->sparkle_count += sparkle_cluster(p->sparkles + p->sparkle_count, room, (int)(s->bottle_w * 0.4f));
    }
}

static void tick_sparkles(shelf_state *s) {
    if (IsIconic(s->win->hwnd)) {
        return;
    }
    int any = 0;
    for (int i = 0; i < s->pour_count; i++) {
        pour_item *p = &s->pours[i];
        if (p->sparkle_count == 0) {
            continue;
        }
        if (!p->visible || !p->enabled) {
            p->sparkle_count = 0;
            any = 1;
            continue;
        }
        int alive = 0;
        for (int k = 0; k < p->sparkle_count; k++) {
            p->sparkles[k].frame++;
            if (p->sparkles[k].frame >= SPARKLE_FRAMES) {
                continue;
            }
            p->sparkles[alive++] = p->sparkles[k];
        }
        p->sparkle_count = alive;
        any = 1;
    }
    if (any) {
        ui_invalidate(s->win);
    }
}

static void shelf_timer(ui_window *w, UINT_PTR id) {
    shelf_state *s = (shelf_state *)w->self;
    if (id == TIMER_TICK) {
        tick_sparkles(s);
    } else if (id == TIMER_SPAWN) {
        spawn_sparkles(s);
    }
}

static void shelf_dpi(ui_window *w) {
    shelf_state *s = (shelf_state *)w->self;
    show_preview(s);
}

static void shelf_destroyed(ui_window *w) {
    shelf_state *s = (shelf_state *)w->self;
    KillTimer(w->hwnd, TIMER_TICK);
    KillTimer(w->hwnd, TIMER_SPAWN);
    stop_audio(s);
    free_pours(s);
    free_previews(s);
    buf_free(&s->audio);
    ui_image_free(s->preview_image);
    for (int i = 0; i < s->art_count; i++) {
        ui_image_free(s->art[i].image);
    }
    bottle_base_free(&s->base);
    for (int i = 0; i < T_COUNT; i++) {
        button_free(&s->top[i]);
    }
    for (int i = 0; i < S_COUNT; i++) {
        button_free(&s->side[i]);
    }
    log_free(&s->desc);
    log_free(&s->log);
    free(s->side_panel.title);
    free(s->title);
    free(s->meta);
    free(s->clash_note);
    free(s->preview_text);
    if (s->tab != NULL) {
        tab_free(s->tab);
        free(s->tab);
    }
    gamedesc_free(s->game);
    free(s->game_dir);
    free(s->mods_dir);
    if (s->app->shelf == w->hwnd) {
        s->app->shelf = NULL;
    }
    free(s);
}

static const ui_handlers shelf_handlers = {
    shelf_paint, shelf_move, shelf_down, shelf_up, shelf_leave, shelf_wheel, shelf_timer, shelf_progress,
    shelf_done, shelf_dpi, NULL, NULL, shelf_destroyed,
};

static void layout_side(shelf_state *s) {
    float ox = side_x();
    float oy = SHELF_TOP;
    float height = shelf_h();
    float spare = height - SHELF_PAD - LOG_MIN - 34 - SHELF_PAD;
    float panel_h = spare > SHELF_DETAILS ? spare : SHELF_DETAILS;
    panel_init(&s->side_panel, ox + SHELF_PAD / 2, oy + SHELF_PAD, SHELF_SIDE - SHELF_PAD, panel_h, "Bottle Details");
    float desc_h = (SHELF_PAD + panel_h) - DESC_Y - 12;
    if (desc_h < DESC_MIN) {
        desc_h = DESC_MIN;
    }
    log_place(&s->desc, ox + SHELF_PAD, oy + DESC_Y, SHELF_SIDE - SHELF_PAD * 2, desc_h);
    progress_init(&s->progress, ox + SHELF_PAD, oy + SHELF_PAD + panel_h + 12, SHELF_SIDE - SHELF_PAD * 2, 10);
    float log_y = SHELF_PAD + panel_h + 34;
    float log_h = height - log_y - SHELF_PAD;
    log_place(&s->log, ox + SHELF_PAD / 2, oy + log_y, SHELF_SIDE - SHELF_PAD, log_h > 48 ? log_h : 48);
}

HWND shelf_open(gs_app *app, HWND owner, const gs_game *game, const char *game_dir, char *why, size_t room) {
    if (game == NULL || game_dir == NULL) {
        snprintf(why, room, "Set the game folder before opening the mod manager.");
        return NULL;
    }
    shelf_state *s = (shelf_state *)calloc(1, sizeof(shelf_state));
    if (s == NULL) {
        snprintf(why, room, "out of memory");
        return NULL;
    }
    s->app = app;
    s->selected = -1;
    s->game = gamedesc_load(game->game_id);
    s->game_dir = gs_path_normalize(game_dir);
    s->mods_dir = gs_mods_dir();
    s->tab = (gs_tab *)calloc(1, sizeof(gs_tab));
    if (s->game == NULL || s->game_dir == NULL || s->mods_dir == NULL || s->tab == NULL ||
        !tab_open(s->tab, NULL, s->game_dir, s->game->game_id)) {
        snprintf(why, room, "Couldnt open the %s tab.", game->game_id);
        free(s->tab);
        gamedesc_free(s->game);
        free(s->game_dir);
        free(s->mods_dir);
        free(s);
        return NULL;
    }
    err e;
    err_clear(&e);
    if (!returns_capture_sizes(s->game, s->game_dir, s->tab, &s->measured_now, &e)) {
        snprintf(why, room, "%s", e.text);
        tab_free(s->tab);
        free(s->tab);
        gamedesc_free(s->game);
        free(s->game_dir);
        free(s->mods_dir);
        free(s);
        return NULL;
    }
    if (!bottle_base_load(&s->base, RES_BOTTLE)) {
        snprintf(why, room, "Missing artwork: bottle.png");
        tab_free(s->tab);
        free(s->tab);
        gamedesc_free(s->game);
        free(s->game_dir);
        free(s->mods_dir);
        free(s);
        return NULL;
    }
    audio_init(&s->player);
    buf_init(&s->audio);
    s->audio_enabled = settings_get_bool(&app->settings, "mod_audio", 1);
    s->bottle_w = bottle_width_for(&s->base, BOTTLE_H);
    s->label_width = (float)(int)((BOTTLE_LABEL_X1 - BOTTLE_LABEL_X0) * s->bottle_w) - 4;
    s->label_offset = (float)(((BOTTLE_LABEL_Y0 + BOTTLE_LABEL_Y1) / 2 - 0.5) * BOTTLE_H);
    const gs_theme *t = app->theme;
    const char *labels[T_COUNT] = {"Rescan", "Pour", "Empty", "Empty Every Bottle", "Open Mods Folder"};
    ui_action actions[T_COUNT] = {on_rescan, enable_selected, disable_selected, disable_all, open_mods_folder};
    ui_tone tones[T_COUNT] = {TONE_NORMAL, TONE_ACCENT, TONE_NORMAL, TONE_DANGER, TONE_NORMAL};
    float cursor = SHELF_PAD;
    ui_font *button_font = ui_font_get(L"Segoe UI", 10, 1);
    for (int i = 0; i < T_COUNT; i++) {
        wchar_t *wide = ui_wide(labels[i]);
        float width = (float)(int)ui_measure(button_font, wide, (int)wcslen(wide)) + 30;
        free(wide);
        button_init(&s->top[i], cursor, (SHELF_TOP - SHELF_BUTTON_H) / 2.0f, width, SHELF_BUTTON_H, labels[i],
                    actions[i], s, tones[i]);
        s->top_list[i] = &s->top[i];
        cursor += width + SHELF_BUTTON_GAP;
    }
    float ox = side_x();
    float oy = SHELF_TOP;
    button_init(&s->side[S_PREV], ox + SHELF_PAD, oy + NAV_Y, 54, 26, "<", on_prev, s, TONE_NORMAL);
    button_init(&s->side[S_NEXT], ox + SHELF_PAD + 122, oy + NAV_Y, 54, 26, ">", on_next, s, TONE_NORMAL);
    button_init(&s->side[S_AUDIO], ox + SHELF_PAD + PREVIEW_W - 128, oy + NAV_Y, 128, 26,
                s->audio_enabled ? "Music: On" : "Music: Off", on_audio, s, TONE_NORMAL);
    for (int i = 0; i < S_COUNT; i++) {
        s->side_list[i] = &s->side[i];
    }
    log_init(&s->desc, t, ox + SHELF_PAD, oy + DESC_Y, SHELF_SIDE - SHELF_PAD * 2, DESC_MIN, L"Segoe UI", 9, 0);
    log_init(&s->log, t, ox + SHELF_PAD / 2, oy + SHELF_PAD + SHELF_DETAILS + 34, SHELF_SIDE - SHELF_PAD, 160,
             L"Consolas", 9, 1);
    layout_side(s);
    set_wide(&s->title, "Nothing selected");
    set_preview_text(s, "No bottle picked", 1);
    char title[300];
    snprintf(title, sizeof(title), "Mod Shelf, %s", s->game->display_name);
    wchar_t *wide_title = ui_wide(title);
    s->win = ui_window_create(wide_title, SHELF_WIDTH, SHELF_HEIGHT, owner, t, &shelf_handlers, s);
    free(wide_title);
    if (s->win == NULL) {
        snprintf(why, room, "Couldnt create the Mod Shelf window.");
        return NULL;
    }
    s->columns = columns_for(s, shelf_w());
    rescan(s);
    SetTimer(s->win->hwnd, TIMER_TICK, SPARKLE_TICK, NULL);
    SetTimer(s->win->hwnd, TIMER_SPAWN, SPARKLE_SPAWN, NULL);
    spawn_sparkles(s);
    ShowWindow(s->win->hwnd, SW_SHOW);
    UpdateWindow(s->win->hwnd);
    return s->win->hwnd;
}
