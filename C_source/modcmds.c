#include "modcmds.h"
#include "diag.h"
#include "gamedesc.h"
#include "gsfs.h"
#include "ledger.h"
#include "recipe.h"
#include "returns.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    gs_game *game;
    const char *game_dir;
    gs_tab tab;
    int tab_open;
} mod_scope;

static void job_log_sink(void *ctx, const char *level, const char *text) {
    emit_log((job_ctx *)ctx, level, "%s", text);
}

static void job_progress(void *ctx, int64_t done, int64_t total) {
    emit_progress((job_ctx *)ctx, done, total, "pouring");
}

static int open_scope(const json_value *req, mod_scope *scope, int want_tab, err *e) {
    memset(scope, 0, sizeof(*scope));
    const char *game_id = json_as_str(json_obj_get(req, "game"), NULL);
    scope->game_dir = json_as_str(json_obj_get(req, "dir"), NULL);
    if (game_id == NULL || scope->game_dir == NULL) {
        err_set(e, "this needs a game and a dir");
        return 0;
    }
    scope->game = gamedesc_load(game_id);
    if (scope->game == NULL) {
        err_set(e, "unknown game: %s", game_id);
        return 0;
    }
    if (want_tab) {
        const char *taildata = json_as_str(json_obj_get(req, "taildata"), NULL);
        if (!tab_open(&scope->tab, taildata, scope->game_dir, scope->game->game_id)) {
            err_set(e, "couldnt open the %s tab", scope->game->game_id);
            gamedesc_free(scope->game);
            scope->game = NULL;
            return 0;
        }
        scope->tab_open = 1;
    }
    return 1;
}

static void close_scope(mod_scope *scope) {
    if (scope->tab_open) {
        tab_free(&scope->tab);
    }
    gamedesc_free(scope->game);
    memset(scope, 0, sizeof(*scope));
}

static void write_asset(json_writer *w, const recipe_asset *asset) {
    jw_obj_open(w);
    jw_kv_str(w, "name", asset->name);
    jw_kv_i64(w, "size", asset->size);
    jw_kv_str(w, "sha256", asset->sha256);
    jw_obj_close(w);
}

static void write_recipe(json_writer *w, const gs_recipe *r) {
    jw_obj_open(w);
    jw_kv_str(w, "mod_id", r->mod_id);
    jw_kv_str(w, "path", r->path);
    jw_kv_str(w, "name", r->name);
    jw_kv_str(w, "game", r->game);
    jw_kv_i64(w, "game_index", r->game_index);
    jw_kv_i64(w, "version", r->version);
    jw_kv_str(w, "author", r->author);
    jw_kv_str(w, "mod_version", r->version_text);
    jw_kv_str(w, "description", r->description);
    jw_kv_str(w, "color", r->colour);
    jw_kv_i64(w, "data_start", r->data_start);
    jw_kv_i64(w, "payload_bytes", r->payload_bytes);
    jw_kv_i64(w, "images_start", r->images_start);
    jw_kv_i64(w, "audio_start", r->audio_start);
    jw_key(w, "entries");
    jw_arr_open(w);
    for (size_t i = 0; i < r->entry_count; i++) {
        const recipe_entry *entry = &r->entries[i];
        jw_obj_open(w);
        jw_kv_i64(w, "idx_marker", entry->idx_marker);
        jw_kv_i64(w, "entry_off", entry->entry_off);
        jw_kv_i64(w, "comp_marker", entry->comp_marker);
        jw_kv_i64(w, "payload_size", entry->size);
        jw_kv_str(w, "payload_sha256", entry->digest);
        jw_kv_str(w, "container", entry->container);
        jw_obj_close(w);
    }
    jw_arr_close(w);
    jw_key(w, "images");
    jw_arr_open(w);
    for (size_t i = 0; i < r->image_count; i++) {
        write_asset(w, &r->images[i]);
    }
    jw_arr_close(w);
    jw_key(w, "audio");
    if (r->has_audio) {
        write_asset(w, &r->audio);
    } else {
        jw_null(w);
    }
    jw_obj_close(w);
}

static int cmd_paths(job_ctx *job) {
    char *taildata = gs_taildata_dir();
    char *filenames = gs_filenames_dir();
    char *mods = gs_mods_dir();
    char *log = gs_log_path();
    char *pngs = gs_png_dir();
    json_writer w;
    emit_ok_open(job, &w);
    jw_obj_open(&w);
    jw_kv_str(&w, "package_root", gs_package_root());
    jw_kv_str(&w, "project_root", gs_project_root());
    jw_kv_str(&w, "taildata", taildata);
    jw_kv_str(&w, "filenames", filenames);
    jw_kv_str(&w, "mods", mods);
    jw_kv_str(&w, "pngs", pngs);
    jw_kv_str(&w, "log", log);
    jw_obj_close(&w);
    emit_ok_close(job, &w);
    free(taildata);
    free(filenames);
    free(mods);
    free(log);
    free(pngs);
    return 1;
}

static int cmd_recipe(job_ctx *job, const json_value *req, err *e) {
    const char *path = json_as_str(json_obj_get(req, "path"), NULL);
    if (path == NULL) {
        err_set(e, "recipe needs a path");
        return 0;
    }
    gs_recipe r;
    if (!recipe_read(path, &r, e)) {
        recipe_free(&r);
        return 0;
    }
    json_writer w;
    emit_ok_open(job, &w);
    write_recipe(&w, &r);
    emit_ok_close(job, &w);
    recipe_free(&r);
    return 1;
}

static int cmd_scan(job_ctx *job, const json_value *req, err *e) {
    const char *folder = json_as_str(json_obj_get(req, "folder"), NULL);
    char *fallback = NULL;
    if (folder == NULL) {
        fallback = gs_mods_dir();
        folder = fallback;
    }
    if (folder == NULL) {
        err_set(e, "scan needs a folder");
        return 0;
    }
    const char *game_id = json_as_str(json_obj_get(req, "game"), "");
    int game_index = (int)json_as_i64(json_obj_get(req, "game_index"), -1);
    recipe_scan scan;
    recipe_scan_folder(folder, game_id, game_index, &scan);
    json_writer w;
    emit_ok_open(job, &w);
    jw_obj_open(&w);
    jw_key(&w, "recipes");
    jw_arr_open(&w);
    for (size_t i = 0; i < scan.count; i++) {
        write_recipe(&w, &scan.items[i]);
    }
    jw_arr_close(&w);
    jw_key(&w, "problems");
    jw_arr_open(&w);
    for (size_t i = 0; i < scan.problem_count; i++) {
        jw_obj_open(&w);
        jw_kv_str(&w, "path", scan.problem_paths[i]);
        jw_kv_str(&w, "msg", scan.problem_texts[i]);
        jw_obj_close(&w);
    }
    jw_arr_close(&w);
    jw_obj_close(&w);
    emit_ok_close(job, &w);
    recipe_scan_free(&scan);
    free(fallback);
    return 1;
}

static int cmd_tab(job_ctx *job, const json_value *req, err *e) {
    mod_scope scope;
    if (!open_scope(req, &scope, 1, e)) {
        return 0;
    }
    size_t id_count = 0;
    char **ids = tab_enabled_ids(&scope.tab, &id_count);
    json_writer w;
    emit_ok_open(job, &w);
    jw_obj_open(&w);
    jw_kv_str(&w, "path", scope.tab.path);
    jw_kv_str(&w, "game_dir", scope.tab.game_dir);
    jw_kv_bool(&w, "measured", tab_measured(&scope.tab));
    jw_key(&w, "container_sizes");
    jw_obj_open(&w);
    for (size_t i = 0; i < scope.tab.size_count; i++) {
        jw_kv_i64(&w, scope.tab.sizes[i].key, scope.tab.sizes[i].size);
    }
    jw_obj_close(&w);
    jw_kv_i64(&w, "vanilla_slots", (int64_t)scope.tab.vanilla.count);
    jw_key(&w, "enabled");
    jw_arr_open(&w);
    for (size_t i = 0; i < id_count; i++) {
        jw_str(&w, ids[i]);
    }
    jw_arr_close(&w);
    jw_obj_close(&w);
    emit_ok_close(job, &w);
    free(ids);
    close_scope(&scope);
    return 1;
}

static int cmd_measure(job_ctx *job, const json_value *req, err *e) {
    mod_scope scope;
    if (!open_scope(req, &scope, 1, e)) {
        return 0;
    }
    int measured = 0;
    int ok = returns_capture_sizes(scope.game, scope.game_dir, &scope.tab, &measured, e);
    if (ok) {
        json_writer w;
        emit_ok_open(job, &w);
        jw_obj_open(&w);
        jw_kv_bool(&w, "measured", measured);
        jw_obj_close(&w);
        emit_ok_close(job, &w);
    }
    close_scope(&scope);
    return ok;
}

static int cmd_pour(job_ctx *job, const json_value *req, err *e) {
    const char *path = json_as_str(json_obj_get(req, "recipe"), NULL);
    if (path == NULL) {
        err_set(e, "pour needs a recipe");
        return 0;
    }
    mod_scope scope;
    if (!open_scope(req, &scope, 1, e)) {
        return 0;
    }
    gs_recipe r;
    if (!recipe_read(path, &r, e)) {
        recipe_free(&r);
        close_scope(&scope);
        return 0;
    }
    int64_t poured = 0;
    int touched = 0;
    gs_log_set_sink(job_log_sink, job);
    int ok = returns_apply(&r, scope.game, scope.game_dir, &scope.tab, job_progress, job, &poured,
                           &touched, e);
    gs_log_set_sink(NULL, NULL);
    if (!ok && touched) {
        err keep;
        err_clear(&keep);
        tab_save(&scope.tab, &keep);
    }
    if (ok) {
        json_writer w;
        emit_ok_open(job, &w);
        jw_obj_open(&w);
        jw_kv_str(&w, "mod_id", r.mod_id);
        jw_kv_i64(&w, "slots", poured);
        jw_obj_close(&w);
        emit_ok_close(job, &w);
    }
    recipe_free(&r);
    close_scope(&scope);
    return ok;
}

static int cmd_empty(job_ctx *job, const json_value *req, err *e) {
    const char *mod_id = json_as_str(json_obj_get(req, "mod"), NULL);
    if (mod_id == NULL) {
        err_set(e, "empty needs a mod");
        return 0;
    }
    mod_scope scope;
    if (!open_scope(req, &scope, 1, e)) {
        return 0;
    }
    int64_t restored = 0;
    gs_log_set_sink(job_log_sink, job);
    int ok = returns_disable(gs_path_name(mod_id), scope.game, scope.game_dir, &scope.tab, &restored, e);
    gs_log_set_sink(NULL, NULL);
    if (ok) {
        json_writer w;
        emit_ok_open(job, &w);
        jw_obj_open(&w);
        jw_kv_i64(&w, "restored", restored);
        jw_obj_close(&w);
        emit_ok_close(job, &w);
    }
    close_scope(&scope);
    return ok;
}

static int cmd_empty_all(job_ctx *job, const json_value *req, err *e) {
    mod_scope scope;
    if (!open_scope(req, &scope, 1, e)) {
        return 0;
    }
    int64_t restored = 0;
    int64_t trimmed = 0;
    gs_log_set_sink(job_log_sink, job);
    int ok = returns_disable_all(scope.game, scope.game_dir, &scope.tab, &restored, &trimmed, e);
    gs_log_set_sink(NULL, NULL);
    if (ok) {
        json_writer w;
        emit_ok_open(job, &w);
        jw_obj_open(&w);
        jw_kv_i64(&w, "restored", restored);
        jw_kv_i64(&w, "trimmed", trimmed);
        jw_obj_close(&w);
        emit_ok_close(job, &w);
    }
    close_scope(&scope);
    return ok;
}

static int cmd_containers_in(job_ctx *job, const json_value *req, err *e) {
    mod_scope scope;
    if (!open_scope(req, &scope, 0, e)) {
        return 0;
    }
    size_t count = 0;
    char **names = returns_containers_in(scope.game, scope.game_dir, &count);
    json_writer w;
    emit_ok_open(job, &w);
    jw_obj_open(&w);
    jw_key(&w, "containers");
    jw_arr_open(&w);
    for (size_t i = 0; i < count; i++) {
        jw_str(&w, names[i]);
    }
    jw_arr_close(&w);
    jw_obj_close(&w);
    emit_ok_close(job, &w);
    returns_names_free(names, count);
    close_scope(&scope);
    return 1;
}

static int cmd_diagnose(job_ctx *job, const json_value *req, err *e) {
    gs_diagnostics d;
    if (!diag_run(json_as_str(json_obj_get(req, "path"), NULL), &d)) {
        err_set(e, "couldnt run the diagnostics");
        return 0;
    }
    char *report = diag_report_text(&d);
    json_writer w;
    emit_ok_open(job, &w);
    jw_obj_open(&w);
    jw_kv_str(&w, "status", diag_status(&d));
    jw_kv_str(&w, "path", d.path);
    jw_kv_str(&w, "drive", d.drive);
    jw_key(&w, "free");
    if (d.have_space) {
        jw_i64(&w, d.free_bytes);
    } else {
        jw_null(&w);
    }
    jw_key(&w, "total");
    if (d.have_space) {
        jw_i64(&w, d.total_bytes);
    } else {
        jw_null(&w);
    }
    jw_kv_bool(&w, "can_read", d.can_read);
    jw_kv_bool(&w, "can_write", d.can_write);
    jw_kv_bool(&w, "can_create", d.can_create);
    jw_kv_bool(&w, "can_delete", d.can_delete);
    jw_kv_str(&w, "protected", d.protected_name);
    jw_key(&w, "issues");
    jw_arr_open(&w);
    for (size_t i = 0; i < d.issue_count; i++) {
        jw_obj_open(&w);
        jw_kv_str(&w, "severity", d.issues[i].severity);
        jw_kv_str(&w, "msg", d.issues[i].message);
        jw_obj_close(&w);
    }
    jw_arr_close(&w);
    jw_kv_str(&w, "report", report);
    jw_obj_close(&w);
    emit_ok_close(job, &w);
    free(report);
    diag_free(&d);
    return 1;
}

static int cmd_collect(job_ctx *job, const json_value *req, err *e) {
    const char *folder = json_as_str(json_obj_get(req, "folder"), NULL);
    const json_value *records = json_obj_get(req, "records");
    if (folder == NULL || records == NULL) {
        err_set(e, "collect needs a folder and records");
        return 0;
    }
    size_t count = records->kind == JSON_OBJ || records->kind == JSON_ARR ? records->count : 0;
    const char **keys = (const char **)calloc(count + 1, sizeof(const char *));
    if (keys == NULL) {
        err_set(e, "out of memory");
        return 0;
    }
    size_t n = 0;
    for (size_t i = 0; i < count; i++) {
        const char *key = records->kind == JSON_OBJ ? records->keys[i] : json_as_str(records->items[i], NULL);
        if (key != NULL) {
            keys[n++] = key;
        }
    }
    recipe_match *matches = NULL;
    size_t found = 0;
    int ok = recipe_collect_sources(folder, keys, n, &matches, &found);
    free(keys);
    if (!ok) {
        err_set(e, "couldnt walk %s", folder);
        return 0;
    }
    json_writer w;
    emit_ok_open(job, &w);
    jw_obj_open(&w);
    jw_key(&w, "matches");
    jw_arr_open(&w);
    for (size_t i = 0; i < found; i++) {
        jw_obj_open(&w);
        jw_kv_str(&w, "key", matches[i].key);
        jw_kv_str(&w, "path", matches[i].path);
        jw_obj_close(&w);
    }
    jw_arr_close(&w);
    jw_obj_close(&w);
    emit_ok_close(job, &w);
    recipe_matches_free(matches, found);
    return 1;
}

int modcmds_dispatch(job_ctx *job, const json_value *req, const char *cmd, err *e) {
    if (strcmp(cmd, "paths") == 0) {
        return cmd_paths(job);
    }
    if (strcmp(cmd, "recipe") == 0) {
        return cmd_recipe(job, req, e);
    }
    if (strcmp(cmd, "scan") == 0) {
        return cmd_scan(job, req, e);
    }
    if (strcmp(cmd, "tab") == 0) {
        return cmd_tab(job, req, e);
    }
    if (strcmp(cmd, "measure") == 0) {
        return cmd_measure(job, req, e);
    }
    if (strcmp(cmd, "pour") == 0) {
        return cmd_pour(job, req, e);
    }
    if (strcmp(cmd, "empty") == 0) {
        return cmd_empty(job, req, e);
    }
    if (strcmp(cmd, "empty_all") == 0) {
        return cmd_empty_all(job, req, e);
    }
    if (strcmp(cmd, "containers_in") == 0) {
        return cmd_containers_in(job, req, e);
    }
    if (strcmp(cmd, "diagnose") == 0) {
        return cmd_diagnose(job, req, e);
    }
    if (strcmp(cmd, "collect") == 0) {
        return cmd_collect(job, req, e);
    }
    return -1;
}
