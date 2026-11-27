#include "gui.h"
#include "codec.h"
#include "gsfs.h"
#include "pkg.h"
#include "gsjson.h"
#include "recipe.h"
#include "schema.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define CREATOR_WIDTH 780
#define CREATOR_HEIGHT 860
#define C_PAD 18
#define C_ROW 30
#define C_LABEL 104
#define C_BUTTON 30
#define C_INNER (CREATOR_WIDTH - C_PAD * 2)
#define C_X (C_PAD + 16)
#define C_FIELD_X (C_X + C_LABEL)
#define C_FIELD_W (C_INNER - C_LABEL - 150)
#define C_TOP 48
#define C_LIQUID_MENU 4000
#define C_EDIT_BASE 3000
#define C_LIQUID_COUNT 8

enum { E_SOURCE, E_NAME, E_AUTHOR, E_VERSION, E_DESC, E_COUNT };
enum { CB_BROWSE, CB_IMAGES, CB_AUDIO, CB_RECHECK, CB_BUILD, CB_CLEAR, CB_COUNT };

static const struct {
    const char *name;
    uint32_t colour;
    const char *hex;
} liquids[C_LIQUID_COUNT] = {
    {"Merlot", 0x7B1E3A, "#7B1E3A"}, {"Rose", 0xC25A7C, "#C25A7C"},     {"Amber", 0xC88A3C, "#C88A3C"},
    {"Absinthe", 0x7BC24A, "#7BC24A"}, {"Curacao", 0x3C8AC8, "#3C8AC8"}, {"Violette", 0x8E5BC8, "#8E5BC8"},
    {"Midori", 0x48B08A, "#48B08A"}, {"Sakura", 0xE26F9E, "#E26F9E"},
};

typedef struct {
    float x, y, w, h;
} edit_box;

typedef struct {
    ui_window *win;
    gs_app *app;
    gs_game *game;
    char *unpack_root;
    char *manifest_path;
    char *mods_dir;
    arena records_arena;
    json_value *records;
    const char **record_keys;
    size_t record_count;
    recipe_match *matched;
    size_t matched_count;
    char *source_folder;
    char **images;
    size_t image_count;
    char *audio_path;
    int busy;
    HWND edits[E_COUNT];
    edit_box boxes[E_COUNT];
    HWND focused;
    HFONT font10;
    HFONT font9;
    HBRUSH field_brush;
    int liquid;
    float liquid_y;
    float images_y;
    float audio_y;
    float matched_y;
    float note_y;
    float version_label_x;
    float row_ys[9];
    ui_panel form;
    ui_button buttons[CB_COUNT];
    ui_button *button_list[CB_COUNT];
    ui_progress progress;
    ui_log log;
    char images_text[1200];
    int images_set;
    char audio_text[400];
    int audio_set;
    char matched_text[200];
    ui_ink matched_ink;
    char header[400];
    char note[1200];
    int liquid_hover;
} creator_state;

typedef struct {
    arena a;
    char game_id[32];
    char out_path[4096];
    json_value *meta;
    json_value *entries;
    json_value *images;
    char *audio;
    bottle_stats stats;
} creator_work;

static COLORREF to_colorref(uint32_t rgb) {
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

static void say(creator_state *c, const char *text, ui_ink ink) {
    log_write(&c->log, text, ink);
    ui_invalidate(c->win);
}

static char *edit_text(HWND edit) {
    int len = GetWindowTextLengthW(edit);
    wchar_t *wide = (wchar_t *)calloc((size_t)len + 1, sizeof(wchar_t));
    if (wide == NULL) {
        return _strdup("");
    }
    GetWindowTextW(edit, wide, len + 1);
    char *utf8 = wide_to_utf8(wide);
    free(wide);
    if (utf8 == NULL) {
        return _strdup("");
    }
    size_t at = 0;
    for (size_t i = 0; utf8[i]; i++) {
        if (utf8[i] == '\r' && utf8[i + 1] == '\n') {
            continue;
        }
        utf8[at++] = utf8[i];
    }
    utf8[at] = 0;
    return utf8;
}

static char *strip(char *text) {
    char *start = text;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r' || *start == '\v' || *start == '\f') {
        start++;
    }
    size_t len = strlen(start);
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t' || start[len - 1] == '\n' ||
                       start[len - 1] == '\r' || start[len - 1] == '\v' || start[len - 1] == '\f')) {
        len--;
    }
    memmove(text, start, len);
    text[len] = 0;
    return text;
}

static void set_edit(HWND edit, const char *text) {
    wchar_t *wide = ui_wide(text);
    SetWindowTextW(edit, wide);
    free(wide);
}

static void make_fonts(creator_state *c) {
    if (c->font10 != NULL) {
        DeleteObject(c->font10);
    }
    if (c->font9 != NULL) {
        DeleteObject(c->font9);
    }
    c->font10 = CreateFontW(-MulDiv(10, (int)c->win->dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Segoe UI");
    c->font9 = CreateFontW(-MulDiv(9, (int)c->win->dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Segoe UI");
}

static void place_edits(creator_state *c) {
    for (int i = 0; i < E_COUNT; i++) {
        edit_box *b = &c->boxes[i];
        SetWindowPos(c->edits[i], NULL, ui_px(c->win, b->x + 1), ui_px(c->win, b->y + 1), ui_px(c->win, b->w - 2),
                     ui_px(c->win, b->h - 2), SWP_NOZORDER | SWP_NOACTIVATE);
        SendMessageW(c->edits[i], WM_SETFONT, (WPARAM)(i == E_DESC ? c->font9 : c->font10), TRUE);
        SendMessageW(c->edits[i], EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(ui_px(c->win, 3), ui_px(c->win, 3)));
    }
}

static void refresh_extras(creator_state *c) {
    if (c->image_count > 0) {
        size_t at = 0;
        c->images_text[0] = 0;
        for (size_t i = 0; i < c->image_count; i++) {
            at += (size_t)snprintf(c->images_text + at, sizeof(c->images_text) - at, "%s%s", i > 0 ? ", " : "",
                                   gs_path_name(c->images[i]));
            if (at >= sizeof(c->images_text)) {
                break;
            }
        }
        c->images_set = 1;
    } else {
        snprintf(c->images_text, sizeof(c->images_text), "None chosen");
        c->images_set = 0;
    }
    if (c->audio_path != NULL) {
        snprintf(c->audio_text, sizeof(c->audio_text), "%s", gs_path_name(c->audio_path));
        c->audio_set = 1;
    } else {
        snprintf(c->audio_text, sizeof(c->audio_text), "None chosen");
        c->audio_set = 0;
    }
    ui_invalidate(c->win);
}

static void free_images(creator_state *c) {
    for (size_t i = 0; i < c->image_count; i++) {
        free(c->images[i]);
    }
    free(c->images);
    c->images = NULL;
    c->image_count = 0;
}

static void load_manifest(creator_state *c) {
    arena_init(&c->records_arena);
    json_value *data = c->manifest_path == NULL ? NULL : gsjson_read_file(&c->records_arena, c->manifest_path);
    if (data == NULL) {
        c->records = NULL;
        say(c, "No taildata manifest yet. Unpack the containers first.", INK_DANGER);
        return;
    }
    json_value *files = json_obj_get(data, "files");
    c->records = files != NULL && files->kind == JSON_OBJ ? files : NULL;
    size_t count = c->records == NULL ? 0 : c->records->count;
    c->record_keys = (const char **)calloc(count + 1, sizeof(char *));
    for (size_t i = 0; c->record_keys != NULL && i < count; i++) {
        c->record_keys[i] = c->records->keys[i];
    }
    c->record_count = c->record_keys == NULL ? 0 : count;
}

static void set_busy(creator_state *c, int busy) {
    c->busy = busy;
    c->buttons[CB_BROWSE].enabled = !busy;
    c->buttons[CB_RECHECK].enabled = !busy;
    c->buttons[CB_IMAGES].enabled = !busy;
    c->buttons[CB_AUDIO].enabled = !busy;
    c->buttons[CB_CLEAR].enabled = !busy;
    c->buttons[CB_BUILD].enabled = !busy && c->matched_count > 0;
    ui_invalidate(c->win);
}

static void recheck_source(void *ctx) {
    creator_state *c = (creator_state *)ctx;
    if (c->source_folder == NULL) {
        say(c, "Pick a source folder first.", INK_DANGER);
        return;
    }
    if (c->record_count == 0) {
        say(c, "No manifest to match against. Unpack the containers first.", INK_DANGER);
        return;
    }
    recipe_matches_free(c->matched, c->matched_count);
    c->matched = NULL;
    c->matched_count = 0;
    recipe_collect_sources(c->source_folder, c->record_keys, c->record_count, &c->matched, &c->matched_count);
    char text[600];
    if (c->matched_count > 0) {
        snprintf(c->matched_text, sizeof(c->matched_text), "%zu files match the unpack", c->matched_count);
        c->matched_ink = INK_OK;
        snprintf(text, sizeof(text), "%zu files in this folder match the unpack.", c->matched_count);
        say(c, text, INK_OK);
        for (size_t i = 0; i < c->matched_count && i < 6; i++) {
            snprintf(text, sizeof(text), "   %s", c->matched[i].key);
            say(c, text, INK_MUTED);
        }
        if (c->matched_count > 6) {
            snprintf(text, sizeof(text), "   also %zu more", c->matched_count - 6);
            say(c, text, INK_MUTED);
        }
    } else {
        snprintf(c->matched_text, sizeof(c->matched_text), "Nothing matched");
        c->matched_ink = INK_DANGER;
        say(c, "Nothing in this folder matches the unpacked layout.", INK_DANGER);
    }
    c->buttons[CB_BUILD].enabled = c->matched_count > 0 && !c->busy;
    ui_invalidate(c->win);
}

static void choose_source(void *ctx) {
    creator_state *c = (creator_state *)ctx;
    char *chosen = ui_pick_folder(c->win->hwnd, "Select the folder holding your edited files", c->unpack_root);
    if (chosen == NULL) {
        return;
    }
    free(c->source_folder);
    c->source_folder = gs_path_normalize(chosen);
    free(chosen);
    set_edit(c->edits[E_SOURCE], c->source_folder);
    char *name = strip(edit_text(c->edits[E_NAME]));
    if (name[0] == 0) {
        set_edit(c->edits[E_NAME], gs_path_name(c->source_folder));
    }
    free(name);
    recheck_source(c);
}

static void choose_images(void *ctx) {
    creator_state *c = (creator_state *)ctx;
    char **chosen = NULL;
    size_t count = 0;
    if (!ui_pick_files(c->win->hwnd, "Select preview images", L"Images",
                       L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.ico;*.dds", &chosen, &count)) {
        free(chosen);
        return;
    }
    free_images(c);
    c->images = (char **)calloc(RECIPE_MAX_PREVIEWS, sizeof(char *));
    size_t valid = 0;
    char text[700];
    for (size_t i = 0; i < count; i++) {
        if (!ui_image_decodes(chosen[i])) {
            snprintf(text, sizeof(text), "%s isnt an image Windows can decode, so it was left out.",
                     gs_path_name(chosen[i]));
            say(c, text, INK_DANGER);
            free(chosen[i]);
            continue;
        }
        if (c->images != NULL && c->image_count < RECIPE_MAX_PREVIEWS) {
            c->images[c->image_count++] = chosen[i];
        } else {
            free(chosen[i]);
        }
        valid++;
    }
    free(chosen);
    if (valid > RECIPE_MAX_PREVIEWS) {
        snprintf(text, sizeof(text), "Only the first %d previews are kept.", RECIPE_MAX_PREVIEWS);
        say(c, text, INK_DANGER);
    }
    refresh_extras(c);
}

static void choose_audio(void *ctx) {
    creator_state *c = (creator_state *)ctx;
    char *chosen = ui_pick_file(c->win->hwnd, "Select a WAV to bundle", NULL, NULL, L"WAV audio", L"*.wav");
    if (chosen == NULL) {
        return;
    }
    free(c->audio_path);
    c->audio_path = chosen;
    refresh_extras(c);
}

static void clear_extras(void *ctx) {
    creator_state *c = (creator_state *)ctx;
    free_images(c);
    free(c->audio_path);
    c->audio_path = NULL;
    refresh_extras(c);
    say(c, "Cleared the previews and the theme tune.", INK_MUTED);
}

static void release_work(ui_job *job) {
    creator_work *work = (creator_work *)job->data;
    if (work != NULL) {
        arena_free(&work->a);
        free(work->audio);
        free(work);
    }
    job->data = NULL;
}

static int run_bottle(ui_job *job) {
    creator_work *work = (creator_work *)job->data;
    const game_schema *s = schema_find(work->game_id);
    if (s == NULL) {
        err_set(&job->e, "unknown game: %s", work->game_id);
        return 0;
    }
    int game_index = 0;
    for (int i = 0; i < schema_count(); i++) {
        if (schema_at(i) == s) {
            game_index = i;
            break;
        }
    }
    codec_init();
    codec_set_big_endian(!s->little_endian);
    return pkg_bottle(&job->ctx, s, game_index, work->out_path, work->meta, work->entries, work->images,
                      work->audio, &work->stats, &job->e);
}

static json_value *version_text(arena *a, const json_value *value) {
    if (value == NULL) {
        return jv_str(a, "1.01");
    }
    if (value->kind == JSON_STR) {
        return jv_str(a, value->text);
    }
    buf text;
    buf_init(&text);
    gsjson_dump(value, 0, &text);
    json_value *out = jv_str(a, text.data == NULL ? "" : text.data);
    buf_free(&text);
    return out;
}

static int truthy(const json_value *node) {
    if (node == NULL || node->kind == JSON_NULL) {
        return 0;
    }
    if (node->kind == JSON_STR) {
        return node->text_len > 0;
    }
    if (node->kind == JSON_BOOL) {
        return node->boolean;
    }
    if (node->kind == JSON_NUM) {
        return node->number != 0.0;
    }
    return node->count > 0;
}

static void build_package(void *ctx) {
    creator_state *c = (creator_state *)ctx;
    if (c->busy || c->matched_count == 0) {
        return;
    }
    creator_work *work = (creator_work *)calloc(1, sizeof(creator_work));
    if (work == NULL) {
        return;
    }
    arena_init(&work->a);
    arena *a = &work->a;
    snprintf(work->game_id, sizeof(work->game_id), "%s", c->game->game_id);
    char *name = strip(edit_text(c->edits[E_NAME]));
    if (name[0] == 0) {
        free(name);
        name = _strdup("New Brew");
    }
    path_make_dirs(c->mods_dir);
    char *file_name = (char *)malloc(strlen(name) + 8);
    snprintf(file_name, strlen(name) + 8, "%s%s", name, RECIPE_EXTENSION);
    for (char *ch = file_name; *ch; ch++) {
        if (*ch == ' ') {
            *ch = '_';
        }
    }
    char *out = path_join(c->mods_dir, file_name);
    snprintf(work->out_path, sizeof(work->out_path), "%s", out == NULL ? "" : out);
    free(out);
    free(file_name);

    json_value **items = (json_value **)calloc(c->matched_count + 1, sizeof(json_value *));
    int compressed = 0;
    size_t made = 0;
    for (size_t i = 0; items != NULL && i < c->matched_count; i++) {
        const json_value *record = json_obj_get(c->records, c->matched[i].key);
        char *keys[7];
        json_value *values[7];
        int n = 0;
        keys[n] = "file";
        values[n++] = jv_str(a, c->matched[i].path);
        keys[n] = "idx_marker";
        values[n++] = jv_num(a, (double)json_as_i64(json_obj_get(record, "idx_marker"), 0));
        keys[n] = "entry_off";
        values[n++] = jv_num(a, (double)json_as_i64(json_obj_get(record, "entry_off"), 0));
        const json_value *container = json_obj_get(record, "container");
        if (truthy(container)) {
            keys[n] = "container";
            values[n++] = jv_copy(a, container);
        }
        const json_value *codec = json_obj_get(record, "codec");
        if (truthy(codec)) {
            keys[n] = "codec";
            values[n++] = jv_copy(a, codec);
            keys[n] = "codec_version";
            values[n++] = version_text(a, json_obj_get(record, "codec_version"));
            keys[n] = "codec_chunk";
            values[n++] = jv_num(a, (double)json_as_i64(json_obj_get(record, "codec_chunk"), 0));
            compressed++;
        }
        items[made++] = jv_obj_from(a, keys, values, (size_t)n);
    }
    work->entries = jv_arr_from(a, items, made);
    free(items);

    char *author = strip(edit_text(c->edits[E_AUTHOR]));
    char *version = strip(edit_text(c->edits[E_VERSION]));
    char *description = strip(edit_text(c->edits[E_DESC]));
    char *meta_keys[6] = {"name", "author", "mod_version", "description", "bottle", "color"};
    json_value *meta_values[6] = {
        jv_str(a, name),
        jv_str(a, author[0] ? author : "Unknown"),
        jv_str(a, version[0] ? version : "1.0"),
        jv_str(a, description),
        jv_str(a, "bordeaux"),
        jv_str(a, liquids[c->liquid].hex),
    };
    work->meta = jv_obj_from(a, meta_keys, meta_values, 6);
    free(author);
    free(version);
    free(description);
    json_value *image_items[RECIPE_MAX_PREVIEWS];
    for (size_t i = 0; i < c->image_count; i++) {
        image_items[i] = jv_str(a, c->images[i]);
    }
    work->images = jv_arr_from(a, image_items, c->image_count);
    work->audio = c->audio_path != NULL ? _strdup(c->audio_path) : _strdup("");

    ui_job *job = ui_job_new(c->win->hwnd, 1, 1, run_bottle, release_work, work);
    if (job == NULL) {
        arena_free(&work->a);
        free(work->audio);
        free(work);
        free(name);
        return;
    }
    set_busy(c, 1);
    progress_set(&c->progress, 0.0f);
    char text[700];
    char note[64] = "";
    if (compressed) {
        snprintf(note, sizeof(note), ", %d recompressed", compressed);
    }
    snprintf(text, sizeof(text), "Bottling %s, %zu files%s", name, made, note);
    say(c, text, INK_ACCENT);
    free(name);
    if (!ui_job_start(job)) {
        ui_job_free(job);
        set_busy(c, 0);
    }
}

static void creator_progress(ui_window *w, ui_job *job, int64_t done, int64_t total) {
    creator_state *c = (creator_state *)w->self;
    (void)job;
    if (total > 0 && progress_set(&c->progress, (float)((double)done / (double)total))) {
        ui_invalidate(w);
    }
}

static void creator_done(ui_window *w, ui_job *job) {
    creator_state *c = (creator_state *)w->self;
    creator_work *work = (creator_work *)job->data;
    set_busy(c, 0);
    if (!job->ok) {
        progress_set(&c->progress, 0.0f);
        gs_log("ERROR", "Bottling failed: %s", job->e.text);
        say(c, job->e.text, INK_DANGER);
        ui_message(w->hwnd, "Mod Creator", job->e.text, MB_ICONERROR);
        ui_keep_front(w->hwnd);
        return;
    }
    progress_set(&c->progress, 1.0f);
    char size[48];
    gs_human_size(work->stats.payload_bytes, size, sizeof(size));
    char text[900];
    snprintf(text, sizeof(text), "%s: %lld files, %s, %lld scrambled, %lld previews%s.", gs_path_name(work->out_path),
             (long long)work->stats.entries, size, (long long)work->stats.encrypted_entries,
             (long long)work->stats.images, work->stats.audio_bytes ? ", theme tune bundled" : "");
    say(c, text, INK_OK);
    ui_keep_front(w->hwnd);
}

static void pick_liquid(creator_state *c) {
    HMENU menu = CreatePopupMenu();
    if (menu == NULL) {
        return;
    }
    for (int i = 0; i < C_LIQUID_COUNT; i++) {
        AppendMenuW(menu, MF_OWNERDRAW, (UINT_PTR)(C_LIQUID_MENU + i), (LPCWSTR)(intptr_t)i);
    }
    POINT pt = {ui_px(c->win, C_FIELD_X), ui_px(c->win, c->liquid_y + 26)};
    ClientToScreen(c->win->hwnd, &pt);
    int picked = (int)TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y,
                                      c->win->hwnd, NULL);
    DestroyMenu(menu);
    if (picked >= C_LIQUID_MENU && picked < C_LIQUID_MENU + C_LIQUID_COUNT) {
        c->liquid = picked - C_LIQUID_MENU;
        ui_invalidate(c->win);
    }
}

static int liquid_hit(creator_state *c, float x, float y) {
    return x >= C_FIELD_X && x <= C_FIELD_X + 200 && y >= c->liquid_y && y <= c->liquid_y + 26;
}

static void label(ui_window *w, float x, float y, const char *text) {
    ui_text_utf8(w, x, y + 5, text, ui_font_get(L"Segoe UI", 9, 0), w->theme->text_muted, UI_NW, 0, 0);
}

static void creator_paint(ui_window *w) {
    creator_state *c = (creator_state *)w->self;
    const gs_theme *t = w->theme;
    ui_font *small = ui_font_get(L"Segoe UI", 9, 0);
    ui_text_utf8(w, C_PAD + 2, 20, c->header, small, t->accent, UI_NW, C_INNER, 0);
    panel_draw(w, &c->form);
    label(w, C_X, c->row_ys[0], "Source folder");
    label(w, C_X, c->row_ys[1], "Mod name");
    label(w, C_X, c->row_ys[2], "Author");
    ui_text_utf8(w, c->version_label_x, c->row_ys[2] + 5, "Version", small, t->text_muted, UI_NW, 0, 0);
    label(w, C_X, c->row_ys[3], "Liquid");
    label(w, C_X, c->row_ys[4], "Description");
    label(w, C_X, c->images_y, "Preview images");
    label(w, C_X, c->audio_y, "Theme WAV");
    label(w, C_X, c->matched_y, "Matched files");
    for (int i = 0; i < E_COUNT; i++) {
        edit_box *b = &c->boxes[i];
        ui_fill_rect(w, b->x, b->y, b->x + b->w, b->y + b->h, t->field);
        ui_stroke_rect(w, b->x, b->y, b->x + b->w, b->y + b->h, c->focused == c->edits[i] ? t->accent : t->panel_soft,
                       1.0f);
    }
    float ly = c->liquid_y;
    ui_fill_rect(w, C_FIELD_X, ly, C_FIELD_X + 200, ly + 26, c->liquid_hover ? t->accent : t->panel_soft);
    ui_text_utf8(w, C_FIELD_X + 8, ly + 13, liquids[c->liquid].name, small, c->liquid_hover ? t->bg : t->text, UI_W,
                 0, 0);
    ui_text(w, C_FIELD_X + 190, ly + 13, L"\x25BE", 1, small, c->liquid_hover ? t->bg : t->text, UI_E, 0, 0);
    ui_fill_ellipse(w, C_FIELD_X + 216, ly + 3, C_FIELD_X + 216 + 20, ly + 23, liquids[c->liquid].colour);
    ui_text_utf8(w, C_FIELD_X, c->images_y + 5, c->images_text, small, c->images_set ? t->text : t->text_muted, UI_NW,
                 C_FIELD_W, 0);
    ui_text_utf8(w, C_FIELD_X, c->audio_y + 5, c->audio_text, small, c->audio_set ? t->text : t->text_muted, UI_NW,
                 C_FIELD_W, 0);
    ui_text_utf8(w, C_FIELD_X, c->matched_y + 5, c->matched_text, small, ink_colour(t, c->matched_ink), UI_NW,
                 C_FIELD_W, 0);
    ui_text_utf8(w, C_X, c->note_y, c->note, ui_font_get(L"Segoe UI", 8, 0), t->text_muted, UI_NW, C_INNER - 32, 0);
    for (int i = 0; i < CB_COUNT; i++) {
        button_draw(w, &c->buttons[i]);
    }
    progress_draw(w, t, &c->progress);
    log_draw(w, &c->log);
}

static void creator_move(ui_window *w, float x, float y) {
    creator_state *c = (creator_state *)w->self;
    int changed = buttons_move(c->button_list, CB_COUNT, x, y);
    int hover = liquid_hit(c, x, y);
    if (hover != c->liquid_hover) {
        c->liquid_hover = hover;
        changed = 1;
    }
    int hand = hover;
    for (int i = 0; i < CB_COUNT; i++) {
        hand |= c->buttons[i].hovering;
    }
    w->hand_cursor = hand;
    if (c->log.dragging) {
        log_drag_to(&c->log, y);
        changed = 1;
    }
    if (changed) {
        ui_invalidate(w);
    }
}

static void creator_down(ui_window *w, float x, float y) {
    creator_state *c = (creator_state *)w->self;
    SetFocus(w->hwnd);
    buttons_down(c->button_list, CB_COUNT, x, y);
    if (log_begin_drag(&c->log, x, y)) {
        ui_invalidate(w);
    }
}

static void creator_up(ui_window *w, float x, float y) {
    creator_state *c = (creator_state *)w->self;
    if (c->log.dragging) {
        log_end_drag(&c->log);
    }
    buttons_up(c->button_list, CB_COUNT, x, y);
    if (liquid_hit(c, x, y)) {
        c->liquid_hover = 0;
        pick_liquid(c);
    }
    ui_invalidate(w);
}

static void creator_leave(ui_window *w) {
    creator_state *c = (creator_state *)w->self;
    buttons_leave(c->button_list, CB_COUNT);
    c->liquid_hover = 0;
    w->hand_cursor = 0;
    ui_invalidate(w);
}

static void creator_wheel(ui_window *w, float x, float y, int delta) {
    creator_state *c = (creator_state *)w->self;
    if (log_wheel(&c->log, x, y, delta)) {
        ui_invalidate(w);
    }
}

static LRESULT creator_message(ui_window *w, UINT msg, WPARAM wp, LPARAM lp, int *handled) {
    creator_state *c = (creator_state *)w->self;
    const gs_theme *t = w->theme;
    switch (msg) {
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wp;
            SetTextColor(dc, to_colorref(t->text));
            SetBkColor(dc, to_colorref(t->field));
            *handled = 1;
            return (LRESULT)c->field_brush;
        }
        case WM_COMMAND: {
            int code = HIWORD(wp);
            if (code == EN_SETFOCUS) {
                c->focused = (HWND)lp;
                ui_invalidate(w);
            } else if (code == EN_KILLFOCUS) {
                if (c->focused == (HWND)lp) {
                    c->focused = NULL;
                }
                ui_invalidate(w);
            }
            return 0;
        }
        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT *m = (MEASUREITEMSTRUCT *)lp;
            if (m->CtlType == ODT_MENU) {
                m->itemWidth = (UINT)ui_px(w, 186);
                m->itemHeight = (UINT)ui_px(w, 24);
                *handled = 1;
                return TRUE;
            }
            return 0;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)lp;
            if (d->CtlType != ODT_MENU) {
                return 0;
            }
            int index = (int)d->itemData;
            if (index < 0 || index >= C_LIQUID_COUNT) {
                index = 0;
            }
            int hot = (d->itemState & ODS_SELECTED) != 0;
            HBRUSH bg = CreateSolidBrush(to_colorref(hot ? t->panel_soft : t->panel));
            FillRect(d->hDC, &d->rcItem, bg);
            DeleteObject(bg);
            SetBkMode(d->hDC, TRANSPARENT);
            SetTextColor(d->hDC, to_colorref(liquids[index].colour));
            HGDIOBJ old = SelectObject(d->hDC, c->font9);
            RECT text = d->rcItem;
            text.left += ui_px(w, 10);
            wchar_t *wide = ui_wide(liquids[index].name);
            DrawTextW(d->hDC, wide, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            free(wide);
            SelectObject(d->hDC, old);
            *handled = 1;
            return TRUE;
        }
        default:
            break;
    }
    return 0;
}

static void creator_dpi(ui_window *w) {
    creator_state *c = (creator_state *)w->self;
    make_fonts(c);
    place_edits(c);
}

static void creator_destroyed(ui_window *w) {
    creator_state *c = (creator_state *)w->self;
    ui_unregister_dialog(w->hwnd);
    for (int i = 0; i < CB_COUNT; i++) {
        button_free(&c->buttons[i]);
    }
    log_free(&c->log);
    free(c->form.title);
    recipe_matches_free(c->matched, c->matched_count);
    free(c->record_keys);
    arena_free(&c->records_arena);
    free_images(c);
    free(c->audio_path);
    free(c->source_folder);
    free(c->unpack_root);
    free(c->manifest_path);
    free(c->mods_dir);
    gamedesc_free(c->game);
    if (c->font10 != NULL) {
        DeleteObject(c->font10);
    }
    if (c->font9 != NULL) {
        DeleteObject(c->font9);
    }
    if (c->field_brush != NULL) {
        DeleteObject(c->field_brush);
    }
    if (c->app->creator == w->hwnd) {
        c->app->creator = NULL;
    }
    free(c);
}

static const ui_handlers creator_handlers = {
    creator_paint, creator_move, creator_down, creator_up, creator_leave, creator_wheel, NULL,
    creator_progress, creator_done, creator_dpi, creator_message, NULL, creator_destroyed,
};

static void lay_out(creator_state *c) {
    float y = C_TOP + 44;
    int half = (int)(C_FIELD_W * 0.55);
    c->row_ys[0] = y;
    c->boxes[E_SOURCE] = (edit_box){C_FIELD_X, y, C_FIELD_W, C_ROW - 4};
    button_init(&c->buttons[CB_BROWSE], C_FIELD_X + C_FIELD_W + 10, y, 118, C_BUTTON - 4, "Browse", choose_source, c,
                TONE_NORMAL);
    y += C_ROW + 8;
    c->row_ys[1] = y;
    c->boxes[E_NAME] = (edit_box){C_FIELD_X, y, C_FIELD_W, C_ROW - 4};
    y += C_ROW + 8;
    c->row_ys[2] = y;
    c->boxes[E_AUTHOR] = (edit_box){C_FIELD_X, y, (float)half, C_ROW - 4};
    c->version_label_x = C_FIELD_X + half + 14;
    c->boxes[E_VERSION] = (edit_box){C_FIELD_X + half + 72, y, (float)(C_FIELD_W - half - 72), C_ROW - 4};
    y += C_ROW + 8;
    c->row_ys[3] = y;
    c->liquid_y = y;
    y += C_ROW + 8;
    c->row_ys[4] = y;
    c->boxes[E_DESC] = (edit_box){C_FIELD_X, y, C_FIELD_W, 104};
    y += 118;
    c->images_y = y;
    button_init(&c->buttons[CB_IMAGES], C_FIELD_X + C_FIELD_W + 10, y, 118, C_BUTTON - 4, "Add Images", choose_images,
                c, TONE_NORMAL);
    y += C_ROW + 8;
    c->audio_y = y;
    button_init(&c->buttons[CB_AUDIO], C_FIELD_X + C_FIELD_W + 10, y, 118, C_BUTTON - 4, "Choose WAV", choose_audio, c,
                TONE_NORMAL);
    y += C_ROW + 8;
    c->matched_y = y;
    button_init(&c->buttons[CB_RECHECK], C_FIELD_X + C_FIELD_W + 10, y, 118, C_BUTTON - 4, "Re-check", recheck_source,
                c, TONE_NORMAL);
    y += C_ROW + 10;
    c->note_y = y;
    float actions_y = C_TOP + 492;
    float action_h = C_BUTTON + 6;
    button_init(&c->buttons[CB_BUILD], C_PAD, actions_y, 176, action_h, "Bottle It", build_package, c, TONE_ACCENT);
    button_init(&c->buttons[CB_CLEAR], C_PAD + 186, actions_y, 150, action_h, "Clear Extras", clear_extras, c,
                TONE_NORMAL);
    progress_init(&c->progress, C_PAD + 348, actions_y + 14, C_INNER - 348, 10);
    float log_top = actions_y + action_h + 14;
    log_init(&c->log, c->app->theme, C_PAD, log_top, C_INNER, CREATOR_HEIGHT - log_top - C_PAD, L"Consolas", 9, 1);
    for (int i = 0; i < CB_COUNT; i++) {
        c->button_list[i] = &c->buttons[i];
    }
}

HWND creator_open(gs_app *app, HWND owner, const gs_game *game, const char *unpack_root, char *why, size_t room) {
    if (game == NULL || unpack_root == NULL) {
        snprintf(why, room, "Set the game folder before opening the mod creator.");
        return NULL;
    }
    creator_state *c = (creator_state *)calloc(1, sizeof(creator_state));
    if (c == NULL) {
        snprintf(why, room, "out of memory");
        return NULL;
    }
    c->app = app;
    c->game = gamedesc_load(game->game_id);
    c->unpack_root = _strdup(unpack_root);
    c->manifest_path = gs_taildata_path(game->game_id);
    c->mods_dir = gs_mods_dir();
    snprintf(c->header, sizeof(c->header), "Bottle a folder of edited %s files into one %s package",
             game->display_name, RECIPE_EXTENSION);
    snprintf(c->note, sizeof(c->note), "Bottles are written into %s", c->mods_dir == NULL ? "" : c->mods_dir);
    snprintf(c->matched_text, sizeof(c->matched_text), "Nothing picked yet");
    c->matched_ink = INK_MUTED;
    panel_init(&c->form, C_PAD, C_TOP, C_INNER, 476, "Recipe Card");
    lay_out(c);
    refresh_extras(c);
    c->field_brush = CreateSolidBrush(to_colorref(app->theme->field));
    char title[300];
    snprintf(title, sizeof(title), "Mod Creator, %s", game->display_name);
    wchar_t *wide_title = ui_wide(title);
    c->win = ui_window_create(wide_title, CREATOR_WIDTH, CREATOR_HEIGHT, owner, app->theme, &creator_handlers, c);
    free(wide_title);
    if (c->win == NULL) {
        snprintf(why, room, "Couldnt create the Mod Creator window.");
        return NULL;
    }
    make_fonts(c);
    for (int i = 0; i < E_COUNT; i++) {
        DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
        if (i == E_DESC) {
            style |= ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
        } else {
            style |= ES_AUTOHSCROLL;
        }
        if (i == E_SOURCE) {
            style |= ES_READONLY;
        }
        c->edits[i] = CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 10, 10, c->win->hwnd,
                                      (HMENU)(intptr_t)(C_EDIT_BASE + i), GetModuleHandleW(NULL), NULL);
    }
    set_edit(c->edits[E_VERSION], "1.0");
    place_edits(c);
    ui_register_dialog(c->win->hwnd);
    c->buttons[CB_BUILD].enabled = 0;
    say(c, "Pick the folder holding your edited files to begin.", INK_ACCENT);
    char text[900];
    const char *folder = c->game != NULL ? c->game->unpack_folder : "Game_Unpacked";
    snprintf(text, sizeof(text),
             "The folder has to keep the layout the unpack made. for example %s/Pack_00/modded file because that is "
             "how each file is matched back to its container slot. Modded/%s/Pack_00/modded file works too.",
             folder, folder);
    say(c, text, INK_MUTED);
    load_manifest(c);
    ShowWindow(c->win->hwnd, SW_SHOW);
    UpdateWindow(c->win->hwnd);
    return c->win->hwnd;
}
