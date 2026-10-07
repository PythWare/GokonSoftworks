#include "cmds.h"
#include "codec.h"
#include "pkg.h"
#include "repack.h"
#include "schema.h"
#include "legacy.h"
#include "linkv2.h"
#include "unpack.h"
#include <stdlib.h>
#include <string.h>

static void write_linkv2_parts(json_writer *w, const game_schema *s) {
    const linkv2_game *game = linkv2_game_for(s->game_id);
    jw_key(w, "parts");
    jw_arr_open(w);
    for (int k = 0; game != NULL && k < game->container_count; k++) {
        const linkv2_container *c = &game->containers[k];
        jw_obj_open(w);
        jw_kv_str(w, "container", c->candidates[0]);
        jw_kv_str(w, "toc", c->candidates[0]);
        jw_kv_i64(w, "toc_offset", LINKV2_HEADER_SIZE);
        jw_kv_i64(w, "entry_size", LINKV2_ENTRY_SIZE);
        jw_kv_i64(w, "alignment", (int64_t)1 << game->shift_bits);
        jw_kv_i64(w, "count_offset", 4);
        jw_kv_i64(w, "sector_field", 0);
        jw_kv_i64(w, "offset_shift", game->shift_bits);
        jw_kv_i64(w, "field_size", s->field_size);
        jw_kv_bool(w, "big_endian", !s->little_endian);
        jw_key(w, "fields");
        jw_arr_open(w);
        for (int q = 0; q < s->field_count; q++) {
            jw_str(w, s->fields[q]);
        }
        jw_arr_close(w);
        jw_key(w, "shift_fields");
        jw_arr_open(w);
        for (int q = 0; q < s->shift_field_count; q++) {
            jw_str(w, s->shift_fields[q]);
        }
        jw_arr_close(w);
        jw_kv_str(w, "pack", c->pack);
        jw_kv_bool(w, "named", 1);
        jw_kv_bool(w, "patchable", 1);
        jw_kv_i64(w, "magic", game->magic);
        jw_key(w, "candidates");
        jw_arr_open(w);
        char rel[LINKV2_NAME_MAX];
        for (int q = 0; q < linkv2_path_count(game, c); q++) {
            if (linkv2_path_at(game, c, q, rel, sizeof(rel))) {
                jw_str(w, rel);
            }
        }
        jw_arr_close(w);
        jw_obj_close(w);
    }
    jw_arr_close(w);
}

static int cmd_ping(job_ctx *job) {
    json_writer w;
    emit_ok_open(job, &w);
    jw_obj_open(&w);
    jw_kv_str(&w, "worker", "gokonworks");
    jw_kv_i64(&w, "games", schema_count());
    jw_obj_close(&w);
    emit_ok_close(job, &w);
    return 1;
}

static int cmd_games(job_ctx *job) {
    json_writer w;
    emit_ok_open(job, &w);
    jw_obj_open(&w);
    jw_key(&w, "games");
    jw_arr_open(&w);

    for (int i = 0; i < schema_count(); i++) {
        const game_schema *s = schema_at(i);
        jw_obj_open(&w);
        jw_kv_str(&w, "game_id", s->game_id);
        jw_kv_i64(&w, "game_index", i);
        jw_kv_str(&w, "display_name", s->display_name);
        jw_kv_str(&w, "unpack_folder", s->unpack_folder);
        jw_kv_i64(&w, "entry_size", s->entry_size);
        jw_kv_i64(&w, "field_size", s->field_size);
        jw_kv_bool(&w, "has_cipher", s->has_cipher);
        jw_kv_i64(&w, "family", s->family);
        if (s->family == SCHEMA_FAMILY_LEGACY) {
            jw_key(&w, "parts");
            jw_arr_open(&w);
            for (int k = 0; k < legacy_part_count(s->game_id); k++) {
                const legacy_part *p = legacy_part_at(s->game_id, k);
                jw_obj_open(&w);
                jw_kv_str(&w, "container", p->container);
                jw_kv_str(&w, "toc", p->toc == NULL ? "" : p->toc);
                jw_kv_i64(&w, "toc_offset", p->toc_offset);
                jw_kv_i64(&w, "entry_size", p->entry_size);
                jw_kv_i64(&w, "alignment", LEGACY_SECTOR);
                jw_kv_i64(&w, "count_offset", p->count_offset);
                jw_kv_i64(&w, "sector_field", p->sector_field);
                jw_kv_i64(&w, "offset_shift", p->shift_bits);
                jw_kv_i64(&w, "field_size", p->field_size);
                jw_kv_bool(&w, "big_endian", p->big_endian);
                jw_key(&w, "fields");
                jw_arr_open(&w);
                for (int q = 0; q < p->field_count; q++) {
                    jw_str(&w, p->fields[q]);
                }
                jw_arr_close(&w);
                jw_key(&w, "shift_fields");
                jw_arr_open(&w);
                for (int q = 0; q < p->shift_field_count; q++) {
                    jw_str(&w, p->shift_fields[q]);
                }
                jw_arr_close(&w);
                jw_kv_str(&w, "pack", p->pack);
                jw_kv_bool(&w, "named", p->named);
                jw_kv_bool(&w, "patchable", p->appendable);
                jw_obj_close(&w);
            }
            jw_arr_close(&w);
        }
        if (s->family == SCHEMA_FAMILY_LINKDATA_V2) {
            write_linkv2_parts(&w, s);
        }
        jw_key(&w, "containers");
        jw_arr_open(&w);
        for (int k = 0; k < s->container_count; k++) {
            jw_str(&w, s->containers[k]);
        }
        for (int k = 0; k < linkv2_container_count(s->game_id); k++) {
            jw_str(&w, linkv2_container_at(s->game_id, k)->candidates[0]);
        }
        jw_arr_close(&w);
        jw_key(&w, "idx_files");
        jw_arr_open(&w);
        for (int k = 0; k < s->idx_count; k++) {
            jw_str(&w, s->idx_files[k]);
        }
        jw_arr_close(&w);
        jw_obj_close(&w);
    }

    jw_arr_close(&w);
    jw_obj_close(&w);
    emit_ok_close(job, &w);
    return 1;
}

static int cmd_unpack(job_ctx *job, const json_value *req, err *e) {
    const char *game_id = json_as_str(json_obj_get(req, "game"), NULL);
    const char *base_dir = json_as_str(json_obj_get(req, "dir"), NULL);
    const char *out_root = json_as_str(json_obj_get(req, "out"), NULL);
    const char *state_dir = json_as_str(json_obj_get(req, "state"), NULL);
    const char *ref_dir = json_as_str(json_obj_get(req, "ref"), NULL);
    int write_files = json_as_bool(json_obj_get(req, "write"), 1);

    if (game_id == NULL) {
        err_set(e, "unpack needs a game");
        return 0;
    }
    if (base_dir == NULL) {
        err_set(e, "unpack needs a dir");
        return 0;
    }

    const game_schema *s = schema_find(game_id);
    if (s == NULL) {
        err_set(e, "unknown game: %s", game_id);
        return 0;
    }
    if (!path_is_dir(base_dir)) {
        err_set(e, "not a folder: %s", base_dir);
        return 0;
    }
    if (out_root == NULL) {
        out_root = base_dir;
    }
    if (state_dir == NULL) {
        state_dir = out_root;
    }
    if (!path_make_dirs(state_dir)) {
        err_set(e, "couldnt create the taildata folder: %s", state_dir);
        return 0;
    }

    unpack_opts opts;
    opts.schema = s;
    opts.base_dir = base_dir;
    opts.out_root = out_root;
    opts.state_dir = state_dir;
    opts.ref_dir = ref_dir;
    opts.write_files = write_files;

    unpack_stats stats;
    if (!unpack_run(job, &opts, &stats, e)) {
        return 0;
    }

    char *manifest = taildata_manifest_path(state_dir, s->game_id);

    json_writer w;
    emit_ok_open(job, &w);
    jw_obj_open(&w);
    jw_kv_str(&w, "game", s->game_id);
    jw_kv_str(&w, "unpack_folder", s->unpack_folder);
    jw_kv_str(&w, "manifest", manifest == NULL ? "" : manifest);
    jw_kv_i64(&w, "entries_seen", stats.entries_seen);
    jw_kv_i64(&w, "files_written", stats.files_written);
    jw_kv_i64(&w, "nested_written", stats.nested_written);
    jw_kv_i64(&w, "skipped", stats.skipped);
    jw_kv_i64(&w, "decompress_failures", stats.decompress_failures);
    jw_obj_close(&w);
    emit_ok_close(job, &w);

    free(manifest);
    return 1;
}

static int cmd_repack(job_ctx *job, const json_value *req, err *e) {
    const char *folder = json_as_str(json_obj_get(req, "folder"), NULL);
    const char *original_path = json_as_str(json_obj_get(req, "original"), NULL);
    const char *out_path = json_as_str(json_obj_get(req, "out"), NULL);

    if (folder == NULL || original_path == NULL || out_path == NULL) {
        err_set(e, "repack needs folder, original and out");
        return 0;
    }
    if (!path_is_dir(folder)) {
        err_set(e, "not a folder: %s", folder);
        return 0;
    }

    codec_init();

    buf original;
    buf_init(&original);
    if (!file_read_all(original_path, &original)) {
        buf_free(&original);
        err_set(e, "couldnt read %s", original_path);
        return 0;
    }

    buf rebuilt;
    buf_init(&rebuilt);
    int ok = repack_from_folder(folder, (const unsigned char *)original.data,
                                original.len, &rebuilt, e);
    if (ok) {
        if (!file_write_all(out_path, rebuilt.data, rebuilt.len)) {
            err_set(e, "couldnt write %s", out_path);
            ok = 0;
        }
    }

    if (ok) {
        int unchanged = rebuilt.len == original.len &&
                        (rebuilt.len == 0 ||
                         memcmp(rebuilt.data, original.data, rebuilt.len) == 0);
        json_writer w;
        emit_ok_open(job, &w);
        jw_obj_open(&w);
        jw_kv_str(&w, "out", out_path);
        jw_kv_i64(&w, "original_size", (int64_t)original.len);
        jw_kv_i64(&w, "rebuilt_size", (int64_t)rebuilt.len);
        jw_kv_bool(&w, "unchanged", unchanged);
        jw_obj_close(&w);
        emit_ok_close(job, &w);
    }

    buf_free(&rebuilt);
    buf_free(&original);
    return ok;
}

static int cmd_rebuild(job_ctx *job, const json_value *req, err *e) {
    const char *game_id = json_as_str(json_obj_get(req, "game"), NULL);
    const char *base_dir = json_as_str(json_obj_get(req, "dir"), NULL);
    const char *src_root = json_as_str(json_obj_get(req, "src"), NULL);
    const char *out_dir = json_as_str(json_obj_get(req, "out"), NULL);
    const char *ref_dir = json_as_str(json_obj_get(req, "ref"), NULL);

    if (game_id == NULL || base_dir == NULL || src_root == NULL || out_dir == NULL) {
        err_set(e, "rebuild needs game, dir, src and out");
        return 0;
    }
    if (!path_is_dir(base_dir)) {
        err_set(e, "not a folder: %s", base_dir);
        return 0;
    }

    const game_schema *s = schema_find(game_id);
    if (s == NULL) {
        err_set(e, "unknown game: %s", game_id);
        return 0;
    }
    if (legacy_part_count(s->game_id) == 0) {
        err_set(e, "%s has no containers that are rebuilt rather than appended to",
                s->display_name);
        return 0;
    }

    legacy_rebuild_stats stats;
    if (!legacy_rebuild(job, s->game_id, base_dir, src_root, s->unpack_folder,
                        out_dir, ref_dir, &stats, e)) {
        return 0;
    }

    json_writer w;
    emit_ok_open(job, &w);
    jw_obj_open(&w);
    jw_kv_str(&w, "game", s->game_id);
    jw_kv_str(&w, "out", out_dir);
    jw_key(&w, "rebuilt");
    jw_arr_open(&w);
    for (int i = 0; i < stats.count; i++) {
        jw_obj_open(&w);
        jw_kv_str(&w, "container", stats.items[i].container);
        jw_kv_str(&w, "path", stats.items[i].out_path);
        jw_kv_i64(&w, "entries", stats.items[i].entries);
        jw_kv_i64(&w, "bytes", stats.items[i].bytes);
        jw_obj_close(&w);
    }
    jw_arr_close(&w);
    jw_obj_close(&w);
    emit_ok_close(job, &w);
    return 1;
}

static int cmd_bottle(job_ctx *job, const json_value *req, err *e) {
    const char *game_id = json_as_str(json_obj_get(req, "game"), NULL);
    const char *out_path = json_as_str(json_obj_get(req, "out"), NULL);
    const json_value *meta = json_obj_get(req, "meta");
    const json_value *entries = json_obj_get(req, "entries");

    if (game_id == NULL || out_path == NULL || entries == NULL) {
        err_set(e, "bottle needs game, out and entries");
        return 0;
    }
    const game_schema *s = schema_find(game_id);
    if (s == NULL) {
        err_set(e, "unknown game: %s", game_id);
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

    const json_value *images = json_obj_get(req, "images");
    const char *audio_path = json_as_str(json_obj_get(req, "audio"), NULL);

    bottle_stats stats;
    if (!pkg_bottle(job, s, game_index, out_path, meta, entries, images, audio_path, &stats, e)) {
        return 0;
    }

    json_writer w;
    emit_ok_open(job, &w);
    jw_obj_open(&w);
    jw_kv_str(&w, "out", out_path);
    jw_kv_str(&w, "game", s->game_id);
    jw_kv_i64(&w, "entries", stats.entries);
    jw_kv_i64(&w, "payload_bytes", stats.payload_bytes);
    jw_kv_i64(&w, "encrypted_entries", stats.encrypted_entries);
    jw_kv_i64(&w, "compressed_entries", stats.compressed_entries);
    jw_kv_i64(&w, "images", stats.images);
    jw_kv_i64(&w, "image_bytes", stats.image_bytes);
    jw_kv_i64(&w, "audio_bytes", stats.audio_bytes);
    jw_kv_i64(&w, "game_index", game_index);
    jw_obj_close(&w);
    emit_ok_close(job, &w);
    return 1;
}

int cmd_dispatch(job_ctx *job, const json_value *req, const char *cmd, arena *a, err *e) {
    (void)a;

    if (strcmp(cmd, "ping") == 0) {
        return cmd_ping(job);
    }
    if (strcmp(cmd, "games") == 0) {
        return cmd_games(job);
    }
    if (strcmp(cmd, "unpack") == 0) {
        return cmd_unpack(job, req, e);
    }
    if (strcmp(cmd, "repack") == 0) {
        return cmd_repack(job, req, e);
    }
    if (strcmp(cmd, "bottle") == 0) {
        return cmd_bottle(job, req, e);
    }
    if (strcmp(cmd, "rebuild") == 0) {
        return cmd_rebuild(job, req, e);
    }

    err_set(e, "unknown cmd: %s", cmd);
    return 0;
}
