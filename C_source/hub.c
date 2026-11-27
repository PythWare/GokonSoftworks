#include "gui.h"
#include "codec.h"
#include "diag.h"
#include "gsfs.h"
#include "legacy.h"
#include "repack.h"
#include "returns.h"
#include "schema.h"
#include "subfolder.h"
#include "unpack.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HUB_WIDTH 1120
#define HUB_HEIGHT 896
#define HUB_PAD 20
#define HUB_HEADER 76
#define HUB_BUTTON_HEIGHT 38
#define HUB_BUTTON_GAP 10
#define HUB_GAUGE_HEIGHT 300
#define HUB_MENU_COLUMNS 2
#define HUB_MAX_GAMES 32
#define HUB_POUR_TIMER 1
#define HUB_PATH 2048

enum { B_UNPACK, B_MANAGER, B_CREATOR, B_FOLDER, B_TAILDATA, B_DIAGNOSTICS, B_REBUILD, B_REPACK, B_REVEAL, B_COUNT };
enum { JOB_UNPACK = 1, JOB_TAILDATA, JOB_REBUILD, JOB_REPACK };

typedef struct {
    ui_window *win;
    gs_app *app;
    gs_game *games[HUB_MAX_GAMES];
    int game_count;
    int current;
    ui_panel menu_panel;
    ui_panel pour_panel;
    ui_menu menu;
    ui_gauge gauge;
    char note[64];
    float note_x;
    float note_y;
    ui_button buttons[B_COUNT];
    ui_button *button_list[B_COUNT];
    float pour_target;
    float pour_level;
    int pour_running;
    int busy;
} hub_state;

typedef struct {
    char game_id[32];
    char display[128];
    char unpack_folder[128];
    char base_dir[HUB_PATH];
    char out_root[HUB_PATH];
    char state_dir[HUB_PATH];
    char ref_dir[HUB_PATH];
    char src_root[HUB_PATH];
    char out_dir[HUB_PATH];
    char folder[HUB_PATH];
    char original[HUB_PATH];
    char out_path[HUB_PATH];
    int write_files;
    unpack_stats stats;
    char *manifest;
    legacy_rebuild_stats rebuilt;
    int64_t original_size;
    int64_t rebuilt_size;
    int unchanged;
} hub_work;

static const char *const dynasty[] = {"DW7XL", "DW8XL", "DW8E", "DW9", "DW4H", "DW6", "DW6E"};

static const gs_game *current_game(hub_state *h) {
    return h->current >= 0 && h->current < h->game_count ? h->games[h->current] : NULL;
}

static const char *current_dir(hub_state *h) {
    const gs_game *game = current_game(h);
    return game == NULL ? NULL : settings_game_dir(&h->app->settings, game->game_id);
}

static int has_rebuildable(hub_state *h) {
    const gs_game *game = current_game(h);
    if (game == NULL || !game->has_parts) {
        return 0;
    }
    for (int i = 0; i < game->part_count; i++) {
        if (!game->parts[i].patchable) {
            return 1;
        }
    }
    return 0;
}

static void set_note(hub_state *h, const char *text) {
    snprintf(h->note, sizeof(h->note), "%s", text);
    ui_invalidate(h->win);
}

static void set_buttons_enabled(hub_state *h, int enabled) {
    int keys[] = {B_UNPACK, B_MANAGER, B_CREATOR, B_REVEAL, B_TAILDATA};
    for (int i = 0; i < 5; i++) {
        h->buttons[keys[i]].enabled = enabled;
    }
    h->buttons[B_DIAGNOSTICS].enabled = 1;
    h->buttons[B_REPACK].enabled = 1;
    h->buttons[B_REBUILD].enabled = enabled && has_rebuildable(h);
    ui_invalidate(h->win);
}

static void pour_to(hub_state *h, float fraction, int snap) {
    h->pour_target = fraction < 0 ? 0 : (fraction > 1 ? 1 : fraction);
    if (snap) {
        h->pour_level = h->pour_target;
        gauge_set(&h->gauge, h->pour_level);
        ui_invalidate(h->win);
        return;
    }
    if (!h->pour_running) {
        h->pour_running = 1;
        SetTimer(h->win->hwnd, HUB_POUR_TIMER, 30, NULL);
    }
}

static void pour_step(hub_state *h) {
    float gap = h->pour_target - h->pour_level;
    if (gap < 0.002f && gap > -0.002f) {
        h->pour_level = h->pour_target;
        gauge_set(&h->gauge, h->pour_level);
        KillTimer(h->win->hwnd, HUB_POUR_TIMER);
        h->pour_running = 0;
        ui_invalidate(h->win);
        return;
    }
    h->pour_level += gap * 0.18f;
    if (gauge_set(&h->gauge, h->pour_level)) {
        ui_invalidate(h->win);
    }
}

static void refresh_pour(hub_state *h) {
    const gs_game *game = current_game(h);
    const char *folder = current_dir(h);
    int ready = folder != NULL && path_is_dir(folder);
    set_buttons_enabled(h, ready);
    h->buttons[B_FOLDER].enabled = 1;
    if (!ready) {
        pour_to(h, 0.0f, 1);
        set_note(h, "waiting on a game folder");
        return;
    }
    char *target = game == NULL ? NULL : path_join(gs_project_root(), game->unpack_folder);
    int done = target != NULL && path_is_dir(target);
    free(target);
    pour_to(h, done ? 1.0f : 0.0f, 1);
    set_note(h, done ? "poured" : "not poured yet");
}

static void select_game(void *ctx, const char *game_id) {
    hub_state *h = (hub_state *)ctx;
    int found = -1;
    for (int i = 0; i < h->game_count; i++) {
        if (strcmp(h->games[i]->game_id, game_id) == 0) {
            found = i;
        }
    }
    if (found < 0) {
        return;
    }
    h->current = found;
    settings_set_str(&h->app->settings, "last_game", game_id);
    settings_save(&h->app->settings);
    menu_select(&h->menu, game_id);
    refresh_pour(h);
    ui_invalidate(h->win);
}

static void layout(hub_state *h) {
    float width = HUB_WIDTH;
    float height = HUB_HEIGHT;
    int rows_per_column = (menu_rows_per_page(&h->menu) + HUB_MENU_COLUMNS - 1) / HUB_MENU_COLUMNS;
    float menu_height = rows_per_column * MENU_ROW_HEIGHT + 104;
    float menu_top = HUB_HEADER + HUB_PAD;
    h->menu_panel.x = HUB_PAD;
    h->menu_panel.y = menu_top;
    h->menu_panel.w = width - HUB_PAD * 2;
    h->menu_panel.h = menu_height;
    menu_place(&h->menu, HUB_PAD + 26, menu_top + 74, width - HUB_PAD * 2 - 52, HUB_MENU_COLUMNS);
    float pour_top = menu_top + menu_height + HUB_PAD;
    float pour_height = height - pour_top - HUB_PAD;
    if (pour_height < 200) {
        pour_height = 200;
    }
    h->pour_panel.x = HUB_PAD;
    h->pour_panel.y = pour_top;
    h->pour_panel.w = width - HUB_PAD * 2;
    h->pour_panel.h = pour_height;
    float glass_x = HUB_PAD + 40;
    float glass_y = pour_top + pour_height - h->gauge.height - HUB_PAD;
    gauge_place(&h->gauge, (float)(int)glass_x, (float)(int)glass_y);
    h->note_x = glass_x + h->gauge.width / 2;
    h->note_y = glass_y - 26;
    float button_x = width - HUB_PAD - 240;
    float button_y = pour_top + 34;
    int order[] = {B_UNPACK, B_MANAGER, B_CREATOR, B_FOLDER, B_TAILDATA, B_DIAGNOSTICS, B_REBUILD, B_REPACK, B_REVEAL};
    for (int i = 0; i < B_COUNT; i++) {
        ui_button *b = &h->buttons[order[i]];
        b->x = button_x;
        b->y = button_y;
        b->w = 220;
        b->h = HUB_BUTTON_HEIGHT;
        button_y += HUB_BUTTON_HEIGHT + HUB_BUTTON_GAP;
    }
}

static void begin_job(hub_state *h, const char *note) {
    set_buttons_enabled(h, 0);
    h->buttons[B_FOLDER].enabled = 0;
    pour_to(h, 0.0f, 1);
    set_note(h, note);
}

static void end_job(hub_state *h) {
    h->busy = 0;
    set_buttons_enabled(h, 1);
    h->buttons[B_FOLDER].enabled = 1;
    ui_invalidate(h->win);
}

static void release_work(ui_job *job) {
    hub_work *work = (hub_work *)job->data;
    if (work != NULL) {
        free(work->manifest);
        free(work);
    }
    job->data = NULL;
}

static int run_unpack(ui_job *job) {
    hub_work *work = (hub_work *)job->data;
    const game_schema *s = schema_find(work->game_id);
    if (s == NULL) {
        err_set(&job->e, "unknown game: %s", work->game_id);
        return 0;
    }
    if (!path_is_dir(work->base_dir)) {
        err_set(&job->e, "not a folder: %s", work->base_dir);
        return 0;
    }
    if (!path_make_dirs(work->state_dir)) {
        err_set(&job->e, "couldnt create the taildata folder: %s", work->state_dir);
        return 0;
    }
    unpack_opts opts;
    opts.schema = s;
    opts.base_dir = work->base_dir;
    opts.out_root = work->out_root;
    opts.state_dir = work->state_dir;
    opts.ref_dir = work->ref_dir;
    opts.write_files = work->write_files;
    if (!unpack_run(&job->ctx, &opts, &work->stats, &job->e)) {
        return 0;
    }
    work->manifest = taildata_manifest_path(work->state_dir, s->game_id);
    return 1;
}

static int run_rebuild(ui_job *job) {
    hub_work *work = (hub_work *)job->data;
    if (!path_is_dir(work->base_dir)) {
        err_set(&job->e, "not a folder: %s", work->base_dir);
        return 0;
    }
    const game_schema *s = schema_find(work->game_id);
    if (s == NULL) {
        err_set(&job->e, "unknown game: %s", work->game_id);
        return 0;
    }
    if (legacy_part_count(s->game_id) == 0) {
        err_set(&job->e, "%s has no containers that are rebuilt rather than appended to", s->display_name);
        return 0;
    }
    return legacy_rebuild(&job->ctx, s->game_id, work->base_dir, work->src_root, s->unpack_folder, work->out_dir,
                          work->ref_dir, &work->rebuilt, &job->e);
}

static int run_repack(ui_job *job) {
    hub_work *work = (hub_work *)job->data;
    if (!path_is_dir(work->folder)) {
        err_set(&job->e, "not a folder: %s", work->folder);
        return 0;
    }
    codec_init();
    buf original;
    buf_init(&original);
    if (!file_read_all(work->original, &original)) {
        buf_free(&original);
        err_set(&job->e, "couldnt read %s", work->original);
        return 0;
    }
    buf rebuilt;
    buf_init(&rebuilt);
    int ok = repack_from_folder(work->folder, (const unsigned char *)original.data, original.len, &rebuilt,
                                &job->e);
    if (ok && !file_write_all(work->out_path, rebuilt.data, rebuilt.len)) {
        err_set(&job->e, "couldnt write %s", work->out_path);
        ok = 0;
    }
    if (ok) {
        work->unchanged = rebuilt.len == original.len &&
                          (rebuilt.len == 0 || memcmp(rebuilt.data, original.data, rebuilt.len) == 0);
        work->original_size = (int64_t)original.len;
        work->rebuilt_size = (int64_t)rebuilt.len;
    }
    buf_free(&rebuilt);
    buf_free(&original);
    return ok;
}

static hub_work *new_work(hub_state *h) {
    hub_work *work = (hub_work *)calloc(1, sizeof(hub_work));
    const gs_game *game = current_game(h);
    if (work != NULL && game != NULL) {
        snprintf(work->game_id, sizeof(work->game_id), "%s", game->game_id);
        snprintf(work->display, sizeof(work->display), "%s", game->display_name);
        snprintf(work->unpack_folder, sizeof(work->unpack_folder), "%s", game->unpack_folder);
        const char *dir = current_dir(h);
        snprintf(work->base_dir, sizeof(work->base_dir), "%s", dir == NULL ? "" : dir);
    }
    char *taildata = gs_taildata_dir();
    char *refs = gs_filenames_dir();
    if (work != NULL) {
        snprintf(work->out_root, sizeof(work->out_root), "%s", gs_project_root());
        snprintf(work->state_dir, sizeof(work->state_dir), "%s", taildata == NULL ? "" : taildata);
        snprintf(work->ref_dir, sizeof(work->ref_dir), "%s", refs == NULL ? "" : refs);
    }
    free(taildata);
    free(refs);
    return work;
}

static void launch(hub_state *h, int kind, ui_job_fn run, hub_work *work, const char *note) {
    ui_job *job = ui_job_new(h->win->hwnd, kind, 1, run, release_work, work);
    if (job == NULL) {
        free(work);
        return;
    }
    h->busy = 1;
    begin_job(h, note);
    if (!ui_job_start(job)) {
        ui_job_free(job);
        end_job(h);
        refresh_pour(h);
    }
}

static void start_unpack(void *ctx) {
    hub_state *h = (hub_state *)ctx;
    if (current_game(h) == NULL || current_dir(h) == NULL || h->busy) {
        return;
    }
    hub_work *work = new_work(h);
    if (work == NULL) {
        return;
    }
    work->write_files = 1;
    launch(h, JOB_UNPACK, run_unpack, work, "pouring");
}

static void start_taildata(void *ctx) {
    hub_state *h = (hub_state *)ctx;
    if (current_game(h) == NULL || current_dir(h) == NULL || h->busy) {
        return;
    }
    hub_work *work = new_work(h);
    if (work == NULL) {
        return;
    }
    work->write_files = 0;
    launch(h, JOB_TAILDATA, run_unpack, work, "scanning containers");
}

static void start_rebuild(void *ctx) {
    hub_state *h = (hub_state *)ctx;
    const gs_game *game = current_game(h);
    if (game == NULL || current_dir(h) == NULL || h->busy || !has_rebuildable(h)) {
        return;
    }
    char *unpacked = path_join(gs_project_root(), game->unpack_folder);
    int present = unpacked != NULL && path_is_dir(unpacked);
    free(unpacked);
    if (!present) {
        ui_message(h->win->hwnd, "Rebuild Containers", "Unpack the containers first, there's nothing to rebuild from.",
                   MB_ICONWARNING);
        return;
    }
    hub_work *work = new_work(h);
    if (work == NULL) {
        return;
    }
    snprintf(work->src_root, sizeof(work->src_root), "%s", gs_project_root());
    char name[96];
    snprintf(name, sizeof(name), "%s_Rebuilt", game->game_id);
    char *out = path_join(gs_project_root(), name);
    snprintf(work->out_dir, sizeof(work->out_dir), "%s", out == NULL ? "" : out);
    free(out);
    launch(h, JOB_REBUILD, run_rebuild, work, "rebuilding");
}

static char *stem_of(const char *name) {
    return gs_path_stem(name);
}

static void start_repack(void *ctx) {
    hub_state *h = (hub_state *)ctx;
    if (h->busy) {
        return;
    }
    char *folder = ui_pick_folder(h->win->hwnd, "Select the unpacked subcontainer folder", NULL);
    if (folder == NULL) {
        return;
    }
    char *parent = gs_path_parent(folder);
    const char *expected = gs_path_name(folder);
    gs_names names;
    gs_names_init(&names);
    gs_list_dir(parent, &names);
    char *named = NULL;
    int matches = 0;
    char *first = NULL;
    for (size_t i = 0; i < names.count; i++) {
        char *full = path_join(parent, names.items[i]);
        char *stem = stem_of(names.items[i]);
        if (full != NULL && stem != NULL && subfolder_matches(expected, names.items[i]) && path_is_file(full)) {
            if (first == NULL || strcmp(names.items[i], first) < 0) {
                first = names.items[i];
            }
            matches++;
        }
        free(full);
        free(stem);
    }
    if (matches == 1) {
        named = _strdup(first);
    }
    gs_names_free(&names);
    char title[600];
    if (named != NULL) {
        snprintf(title, sizeof(title), "Select the original file %s", named);
    } else {
        snprintf(title, sizeof(title), "Select the original file named %s", expected);
    }
    char *original = ui_pick_file(h->win->hwnd, title, parent, named != NULL ? named : expected, NULL, NULL);
    if (original == NULL) {
        free(named);
        free(parent);
        free(folder);
        return;
    }
    char *source_stem = stem_of(original);
    if (source_stem == NULL || !subfolder_matches(expected, gs_path_name(original))) {
        char text[2400];
        char from[600];
        if (named != NULL) {
            snprintf(from, sizeof(from), "%s", named);
        } else {
            snprintf(from, sizeof(from), "%s with an extension after it", expected);
        }
        snprintf(text, sizeof(text),
                 "That folder and that file dont belong together, so nothing was written.\n\n"
                 "The folder is named %s, which means it was unpacked from %s.\n\n"
                 "You picked %s.\n\n"
                 "Rebuilding a subcontainer against another one's layout makes a file the game cant read.",
                 expected, from, gs_path_name(original));
        ui_message(h->win->hwnd, "Repack Subcontainer", text, MB_ICONERROR);
        free(source_stem);
        free(original);
        free(named);
        free(parent);
        free(folder);
        return;
    }
    hub_work *work = new_work(h);
    if (work != NULL) {
        snprintf(work->folder, sizeof(work->folder), "%s", folder);
        snprintf(work->original, sizeof(work->original), "%s", original);
        const char *name = gs_path_name(original);
        const char *suffix = name + strlen(source_stem);
        char *source_parent = gs_path_parent(original);
        char out_name[700];
        snprintf(out_name, sizeof(out_name), "%s_repacked%s", source_stem, suffix);
        char *out = source_parent == NULL ? NULL : path_join(source_parent, out_name);
        snprintf(work->out_path, sizeof(work->out_path), "%s", out == NULL ? "" : out);
        free(out);
        free(source_parent);
        launch(h, JOB_REPACK, run_repack, work, "repacking");
    }
    free(source_stem);
    free(original);
    free(named);
    free(parent);
    free(folder);
}

static void choose_game_folder(void *ctx) {
    hub_state *h = (hub_state *)ctx;
    const gs_game *game = current_game(h);
    if (game == NULL) {
        return;
    }
    const char *current = current_dir(h);
    char title[512];
    snprintf(title, sizeof(title), "Where %s lives for %s", game->container_count > 0 ? game->containers[0] : "",
             game->display_name);
    char *chosen = ui_pick_folder(h->win->hwnd, title, current != NULL ? current : gs_project_root());
    if (chosen == NULL) {
        return;
    }
    size_t count = 0;
    char **found = returns_containers_in(game, chosen, &count);
    if (count == 0) {
        buf text;
        buf_init(&text);
        buf_printf(&text, "No containers for %s in that folder.\n\nLooking for: ", game->display_name);
        for (int i = 0; i < game->container_count; i++) {
            buf_printf(&text, "%s%s", i > 0 ? ", " : "", game->containers[i]);
        }
        ui_message(h->win->hwnd, "Game Folder", text.data == NULL ? "" : text.data, MB_ICONWARNING);
        buf_free(&text);
        returns_names_free(found, count);
        free(chosen);
        return;
    }
    returns_names_free(found, count);
    char *normal = gs_path_normalize(chosen);
    settings_set_game_dir(&h->app->settings, game->game_id, normal != NULL ? normal : chosen);
    free(normal);
    settings_save(&h->app->settings);
    free(chosen);
    refresh_pour(h);
}

static void open_diagnostics(void *ctx) {
    hub_state *h = (hub_state *)ctx;
    gs_diagnostics d;
    if (!diag_run(NULL, &d)) {
        return;
    }
    char *text = diag_report_text(&d);
    UINT icon = diag_has_errors(&d) ? MB_ICONERROR : (diag_has_warnings(&d) ? MB_ICONWARNING : MB_ICONINFORMATION);
    ui_message(h->win->hwnd, "Diagnostics", text, icon);
    free(text);
    diag_free(&d);
}

static void open_unpack_folder(void *ctx) {
    hub_state *h = (hub_state *)ctx;
    const gs_game *game = current_game(h);
    if (game == NULL) {
        return;
    }
    char *target = path_join(gs_project_root(), game->unpack_folder);
    if (target == NULL || !path_is_dir(target)) {
        ui_message(h->win->hwnd, "Unpack Folder", "Nothing unpacked yet.", MB_ICONINFORMATION);
    } else {
        ui_open_explorer(target);
    }
    free(target);
}

static void open_mod_manager(void *ctx) {
    hub_state *h = (hub_state *)ctx;
    if (h->app->shelf != NULL && IsWindow(h->app->shelf)) {
        ui_bring_forward(h->app->shelf);
        return;
    }
    char why[512] = "";
    HWND shelf = shelf_open(h->app, h->win->hwnd, current_game(h), current_dir(h), why, sizeof(why));
    if (shelf == NULL) {
        gs_log("ERROR", "couldnt open mod_window: %s", why);
        ui_message(h->win->hwnd, "GokonSoftworks", why[0] ? why : "Couldnt open the mod manager.", MB_ICONERROR);
        return;
    }
    h->app->shelf = shelf;
}

static void open_mod_creator(void *ctx) {
    hub_state *h = (hub_state *)ctx;
    if (h->app->creator != NULL && IsWindow(h->app->creator)) {
        ui_bring_forward(h->app->creator);
        return;
    }
    char why[512] = "";
    HWND creator = creator_open(h->app, h->win->hwnd, current_game(h), gs_project_root(), why, sizeof(why));
    if (creator == NULL) {
        gs_log("ERROR", "couldnt open creator_window: %s", why);
        ui_message(h->win->hwnd, "GokonSoftworks", why[0] ? why : "Couldnt open the mod creator.", MB_ICONERROR);
        return;
    }
    h->app->creator = creator;
}

static void hub_paint(ui_window *w) {
    hub_state *h = (hub_state *)w->self;
    const gs_theme *t = w->theme;
    ui_text(w, HUB_PAD + 4, 16, L"The Menu", 8, ui_font_get(L"Segoe UI", 21, 1), t->text, UI_NW, 0, 0);
    ui_line(w, 0, HUB_HEADER, HUB_WIDTH, HUB_HEADER, t->panel_soft, 1.0f);
    panel_draw(w, &h->menu_panel);
    panel_draw(w, &h->pour_panel);
    menu_draw(w, &h->menu);
    ui_text_utf8(w, h->note_x, h->note_y, h->note, ui_font_get(L"Segoe UI", 9, 0), t->text_muted, UI_N,
                 h->gauge.width + 120, 0);
    gauge_draw(w, &h->gauge);
    for (int i = 0; i < B_COUNT; i++) {
        button_draw(w, &h->buttons[i]);
    }
}

static void hub_move(ui_window *w, float x, float y) {
    hub_state *h = (hub_state *)w->self;
    int changed = buttons_move(h->button_list, B_COUNT, x, y);
    int old_hover = h->menu.hover;
    int over_menu = menu_move(&h->menu, x, y);
    int hand = over_menu;
    for (int i = 0; i < B_COUNT; i++) {
        hand = hand || h->buttons[i].hovering;
    }
    w->hand_cursor = hand;
    if (changed || old_hover != h->menu.hover) {
        ui_invalidate(w);
    }
}

static void hub_down(ui_window *w, float x, float y) {
    hub_state *h = (hub_state *)w->self;
    buttons_down(h->button_list, B_COUNT, x, y);
    if (menu_click(&h->menu, x, y)) {
        ui_invalidate(w);
    }
}

static void hub_up(ui_window *w, float x, float y) {
    hub_state *h = (hub_state *)w->self;
    buttons_up(h->button_list, B_COUNT, x, y);
    ui_invalidate(w);
}

static void hub_leave(ui_window *w) {
    hub_state *h = (hub_state *)w->self;
    buttons_leave(h->button_list, B_COUNT);
    h->menu.hover = -1;
    w->hand_cursor = 0;
    ui_invalidate(w);
}

static void hub_timer(ui_window *w, UINT_PTR id) {
    hub_state *h = (hub_state *)w->self;
    if (id == HUB_POUR_TIMER) {
        pour_step(h);
    }
}

static void hub_progress(ui_window *w, ui_job *job, int64_t done, int64_t total) {
    hub_state *h = (hub_state *)w->self;
    (void)job;
    if (total > 0) {
        pour_to(h, (float)((double)done / (double)total), 0);
    }
}

static void hub_done(ui_window *w, ui_job *job) {
    hub_state *h = (hub_state *)w->self;
    hub_work *work = (hub_work *)job->data;
    if (!job->ok) {
        pour_to(h, 0.0f, 1);
        set_note(h, "spilled");
        end_job(h);
        gs_log("ERROR", "Unpack failed: %s", job->e.text);
        ui_message(w->hwnd, "Unpack", job->e.text, MB_ICONERROR);
        return;
    }
    pour_to(h, 1.0f, 0);
    if (job->kind == JOB_UNPACK) {
        set_note(h, "poured");
        end_job(h);
        return;
    }
    if (job->kind == JOB_TAILDATA) {
        set_note(h, "taildata written");
        end_job(h);
        char seen[48];
        gui_thousands(work->stats.entries_seen, seen, sizeof(seen));
        char text[3000];
        snprintf(text, sizeof(text),
                 "Taildata written for %s.\n\n%s container entries scanned, nothing unpacked.\n\n%s",
                 work->display, seen, work->manifest == NULL ? "" : work->manifest);
        ui_message(w->hwnd, "Generate Taildata", text, MB_ICONINFORMATION);
        return;
    }
    if (job->kind == JOB_REBUILD) {
        set_note(h, "rebuilt");
        end_job(h);
        buf text;
        buf_init(&text);
        buf_puts(&text, "Rebuilt:");
        for (int i = 0; i < work->rebuilt.count; i++) {
            char entries[48];
            char megabytes[64];
            gui_thousands(work->rebuilt.items[i].entries, entries, sizeof(entries));
            gui_thousands_f1((double)work->rebuilt.items[i].bytes / 1000000.0, megabytes, sizeof(megabytes));
            buf_printf(&text, "%s%s: %s entries, %s MB", i == 0 ? "\n  " : "\n  ", work->rebuilt.items[i].container,
                       entries, megabytes);
        }
        if (work->rebuilt.count == 0) {
            buf_puts(&text, "\n  ");
        }
        buf_printf(&text,
                   "\n\nThey keep the same folder layout as the game, so copy the contents over your install.\n\n%s",
                   work->out_dir);
        ui_message(w->hwnd, "Rebuild Containers", text.data, MB_ICONINFORMATION);
        buf_free(&text);
        return;
    }
    if (job->kind == JOB_REPACK) {
        set_note(h, "repacked");
        end_job(h);
        char verdict[400];
        if (work->unchanged) {
            snprintf(verdict, sizeof(verdict), "It came back identical to the original, so it's good.");
        } else {
            char before[48];
            char after[48];
            gui_thousands(work->original_size, before, sizeof(before));
            gui_thousands(work->rebuilt_size, after, sizeof(after));
            snprintf(verdict, sizeof(verdict),
                     "It differs from the original (%s -> %s bytes), expected if you edited anything inside the folder.",
                     before, after);
        }
        char text[4000];
        snprintf(text, sizeof(text), "Rebuilt %s from its folder.\n\n%s\n\nWritten to:\n%s\n\n",
                 gs_path_name(work->original), verdict, work->out_path);
        ui_message(w->hwnd, "Repack Subcontainer", text, MB_ICONINFORMATION);
    }
}

static void hub_destroyed(ui_window *w) {
    hub_state *h = (hub_state *)w->self;
    for (int i = 0; i < B_COUNT; i++) {
        button_free(&h->buttons[i]);
    }
    menu_free(&h->menu);
    gauge_free(&h->gauge);
    free(h->menu_panel.title);
    free(h->pour_panel.title);
    for (int i = 0; i < h->game_count; i++) {
        gamedesc_free(h->games[i]);
    }
    h->app->hub = NULL;
    free(h);
    PostQuitMessage(0);
}

static const ui_handlers hub_handlers = {
    hub_paint, hub_move, hub_down, hub_up, hub_leave, NULL, hub_timer, hub_progress, hub_done,
    NULL, NULL, NULL, hub_destroyed,
};

static void load_games(hub_state *h) {
    for (int i = 0; i < schema_count() && h->game_count < HUB_MAX_GAMES; i++) {
        gs_game *game = gamedesc_load_at(i);
        if (game != NULL) {
            h->games[h->game_count++] = game;
        }
    }
    menu_add_page(&h->menu, "Dynasty Warriors");
    for (size_t k = 0; k < sizeof(dynasty) / sizeof(dynasty[0]); k++) {
        for (int i = 0; i < h->game_count; i++) {
            if (strcmp(h->games[i]->game_id, dynasty[k]) == 0) {
                menu_add_row(&h->menu, h->games[i]->game_id, h->games[i]->display_name, h->games[i]->game_id);
            }
        }
    }
    menu_add_page(&h->menu, "Miscellaneous");
    for (int i = 0; i < h->game_count; i++) {
        int is_dynasty = 0;
        for (size_t k = 0; k < sizeof(dynasty) / sizeof(dynasty[0]); k++) {
            is_dynasty = is_dynasty || strcmp(h->games[i]->game_id, dynasty[k]) == 0;
        }
        if (!is_dynasty) {
            menu_add_row(&h->menu, h->games[i]->game_id, h->games[i]->display_name, h->games[i]->game_id);
        }
    }
    const char *last = settings_get_str(&h->app->settings, "last_game", "");
    h->current = -1;
    for (int i = 0; i < h->game_count; i++) {
        if (strcmp(h->games[i]->game_id, last) == 0) {
            h->current = i;
        }
    }
    if (h->current < 0 && h->game_count > 0) {
        h->current = 0;
    }
    layout(h);
    if (h->current >= 0) {
        select_game(h, h->games[h->current]->game_id);
    }
    layout(h);
}

int hub_open(gs_app *app) {
    hub_state *h = (hub_state *)calloc(1, sizeof(hub_state));
    if (h == NULL) {
        return 0;
    }
    h->app = app;
    const gs_theme *t = app->theme;
    panel_init(&h->menu_panel, 0, 0, 10, 10, "");
    char pour_title[128];
    snprintf(pour_title, sizeof(pour_title), "Fuji's Pour today is %s.", t->name);
    panel_init(&h->pour_panel, 0, 0, 10, 10, pour_title);
    menu_init(&h->menu, select_game, h);
    if (!gauge_init(&h->gauge, 0, 0, HUB_GAUGE_HEIGHT, RES_GLASS)) {
        free(h);
        return 0;
    }
    gauge_caption(&h->gauge, "", 0);
    button_init(&h->buttons[B_UNPACK], 0, 0, 10, HUB_BUTTON_HEIGHT, "Unpack Containers", start_unpack, h, TONE_ACCENT);
    button_init(&h->buttons[B_MANAGER], 0, 0, 10, HUB_BUTTON_HEIGHT, "Mod Manager", open_mod_manager, h, TONE_NORMAL);
    button_init(&h->buttons[B_CREATOR], 0, 0, 10, HUB_BUTTON_HEIGHT, "Mod Creator", open_mod_creator, h, TONE_NORMAL);
    button_init(&h->buttons[B_FOLDER], 0, 0, 10, HUB_BUTTON_HEIGHT, "Set Game Folder", choose_game_folder, h, TONE_NORMAL);
    button_init(&h->buttons[B_TAILDATA], 0, 0, 10, HUB_BUTTON_HEIGHT, "Generate Taildata", start_taildata, h, TONE_NORMAL);
    button_init(&h->buttons[B_REBUILD], 0, 0, 10, HUB_BUTTON_HEIGHT, "Rebuild Containers", start_rebuild, h, TONE_NORMAL);
    button_init(&h->buttons[B_REPACK], 0, 0, 10, HUB_BUTTON_HEIGHT, "Repack Subcontainer", start_repack, h, TONE_NORMAL);
    button_init(&h->buttons[B_DIAGNOSTICS], 0, 0, 10, HUB_BUTTON_HEIGHT, "Diagnostics", open_diagnostics, h, TONE_NORMAL);
    button_init(&h->buttons[B_REVEAL], 0, 0, 10, HUB_BUTTON_HEIGHT, "Open Unpack Folder", open_unpack_folder, h, TONE_NORMAL);
    for (int i = 0; i < B_COUNT; i++) {
        h->button_list[i] = &h->buttons[i];
    }
    h->win = ui_window_create(L"GokonSoftworks", HUB_WIDTH, HUB_HEIGHT, NULL, t, &hub_handlers, h);
    if (h->win == NULL) {
        return 0;
    }
    app->hub = h->win->hwnd;
    set_buttons_enabled(h, 0);
    load_games(h);
    ShowWindow(h->win->hwnd, SW_SHOW);
    UpdateWindow(h->win->hwnd);
    return 1;
}
