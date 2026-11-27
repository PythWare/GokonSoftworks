#define WIN32_LEAN_AND_MEAN
#include "unpack.h"
#include "names.h"
#include "zp1.h"
#include "codec.h"
#include "crypt.h"
#include "legacy.h"
#include "linkv2.h"
#include "nested.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define UNPACK_MAX_THREADS 16
#define UNPACK_LARGE_ENTRY (64u << 20)

typedef struct {
    HANDLE file;
    HANDLE mapping;
    const unsigned char *base;
    int64_t size;
} container_map;

static void container_close(container_map *c) {
    if (c->base != NULL) {
        UnmapViewOfFile((LPCVOID)c->base);
        c->base = NULL;
    }
    if (c->mapping != NULL && c->mapping != INVALID_HANDLE_VALUE) {
        CloseHandle(c->mapping);
        c->mapping = NULL;
    }
    if (c->file != NULL && c->file != INVALID_HANDLE_VALUE) {
        CloseHandle(c->file);
        c->file = NULL;
    }
    c->size = 0;
}

static int container_open(container_map *c, const char *path, err *e) {
    memset(c, 0, sizeof(*c));

    wchar_t *wide = path_to_wide(path);
    if (wide == NULL) {
        err_set(e, "out of memory opening %s", path);
        return 0;
    }

    c->file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wide);
    if (c->file == INVALID_HANDLE_VALUE) {
        err_set(e, "couldnt open %s", path);
        return 0;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(c->file, &size)) {
        container_close(c);
        err_set(e, "couldnt size %s", path);
        return 0;
    }
    if (size.QuadPart == 0) {
        container_close(c);
        err_set(e, "container is empty: %s", path);
        return 0;
    }
    c->size = (int64_t)size.QuadPart;

    c->mapping = CreateFileMappingW(c->file, NULL, PAGE_READONLY, 0, 0, NULL);
    if (c->mapping == NULL) {
        container_close(c);
        err_set(e, "couldnt map %s", path);
        return 0;
    }

    c->base = (const unsigned char *)MapViewOfFile(c->mapping, FILE_MAP_READ, 0, 0, 0);
    if (c->base == NULL) {
        container_close(c);
        err_set(e, "couldnt view %s", path);
        return 0;
    }
    return 1;
}

char *taildata_manifest_path(const char *state_dir, const char *game_id) {
    char name[128];
    snprintf(name, sizeof(name), "gokon_taildata_%s.json", game_id);
    return path_join(state_dir, name);
}

static int manifest_begin(manifest_writer *m, const char *state_dir, const char *unpack_root,
                          const char *game_id, err *e) {
    memset(m, 0, sizeof(*m));

    char *path = taildata_manifest_path(state_dir, game_id);
    if (path == NULL) {
        err_set(e, "out of memory building the manifest path");
        return 0;
    }
    if (!path_make_parent_dirs(path)) {
        free(path);
        err_set(e, "couldnt create the folder for the manifest");
        return 0;
    }

    m->sink = file_open(path, "wb");
    free(path);
    if (m->sink == NULL) {
        err_set(e, "couldnt write the taildata manifest");
        return 0;
    }

    char stamp[32];
    utc_now_iso(stamp);

    jw_init(&m->w, m->sink);
    jw_obj_open(&m->w);
    jw_kv_str(&m->w, "format", TAILDATA_FORMAT);
    jw_kv_i64(&m->w, "version", TAILDATA_VERSION);
    jw_kv_str(&m->w, "game", game_id);
    jw_kv_str(&m->w, "unpack_root", unpack_root);
    jw_kv_str(&m->w, "created_utc", stamp);
    jw_kv_str(&m->w, "updated_utc", stamp);
    jw_key(&m->w, "containers");
    jw_obj_open(&m->w);
    m->open = 1;
    return 1;
}

void manifest_container(manifest_writer *m, int idx_marker, const char *container_path) {
    char key[16];
    snprintf(key, sizeof(key), "%d", idx_marker);
    jw_kv_str(&m->w, key, container_path);
}

void manifest_files_open(manifest_writer *m) {
    jw_obj_close(&m->w);
    jw_key(&m->w, "files");
    jw_obj_open(&m->w);
}

void manifest_record(manifest_writer *m, const char *key, int idx_marker,
                     int64_t entry_off, int comp_marker, const char *container,
                     int64_t entry_index, int64_t unpacked_size, const char *ext,
                     const char *name, int rate, int channels,
                     const char *codec, float codec_version,
                     int64_t codec_chunk) {
    jw_key(&m->w, key);
    jw_obj_open(&m->w);
    jw_kv_i64(&m->w, "idx_marker", idx_marker);
    jw_kv_i64(&m->w, "entry_off", entry_off);
    jw_kv_i64(&m->w, "comp_marker", comp_marker ? 1 : 0);
    jw_kv_str(&m->w, "container", container);
    jw_kv_i64(&m->w, "entry_index", entry_index);
    jw_kv_i64(&m->w, "unpacked_size", unpacked_size);
    jw_kv_str(&m->w, "ext", ext);
    if (name != NULL && name[0] != 0) {
        jw_kv_str(&m->w, "name", name);
    }
    if (rate > 0) {
        jw_kv_i64(&m->w, "sample_rate", rate);
        jw_kv_i64(&m->w, "channels", channels > 0 ? channels : 1);
    }
    if (codec != NULL && codec[0] != 0) {
        jw_kv_str(&m->w, "codec", codec);
        char version[32];
        snprintf(version, sizeof(version), "%.2f", (double)codec_version);
        jw_kv_str(&m->w, "codec_version", version);
        jw_kv_i64(&m->w, "codec_chunk", codec_chunk);
    }
    jw_obj_close(&m->w);
    m->count++;
}

static int manifest_finish(manifest_writer *m) {
    if (!m->open) {
        return 1;
    }
    jw_obj_close(&m->w);
    jw_obj_close(&m->w);
    int ok = jw_finish(&m->w);
    jw_free(&m->w);
    if (m->sink != NULL) {
        if (fclose(m->sink) != 0) {
            ok = 0;
        }
        m->sink = NULL;
    }
    m->open = 0;
    return ok;
}

static void manifest_abort(manifest_writer *m) {
    if (!m->open) {
        return;
    }
    jw_free(&m->w);
    if (m->sink != NULL) {
        fclose(m->sink);
        m->sink = NULL;
    }
    m->open = 0;
}

static CRITICAL_SECTION log_lock;
static CRITICAL_SECTION large_entry_lock;
static int locks_ready;

static void unpack_locks_init(void) {
    if (!locks_ready) {
        InitializeCriticalSection(&log_lock);
        InitializeCriticalSection(&large_entry_lock);
        locks_ready = 1;
    }
}

static void log_failure(const char *state_dir, const char *text) {
    char *path = path_join(state_dir, "gokon_decompress_failures.log");
    if (path == NULL) {
        return;
    }
    EnterCriticalSection(&log_lock);
    FILE *handle = file_open(path, "ab");
    if (handle != NULL) {
        fputs(text, handle);
        fputc('\n', handle);
        fclose(handle);
    }
    LeaveCriticalSection(&log_lock);
    free(path);
}

typedef struct {
    buf work;
    buf staged;
    arena scratch;
} entry_scratch;

static void entry_scratch_init(entry_scratch *sc) {
    memset(sc, 0, sizeof(*sc));
    buf_init(&sc->work);
    buf_init(&sc->staged);
    arena_init(&sc->scratch);
}

static void entry_scratch_free(entry_scratch *sc) {
    buf_free(&sc->work);
    buf_free(&sc->staged);
    arena_free(&sc->scratch);
}

static int decode_entry(const unsigned char *raw, size_t raw_len,
                        uint64_t orig_size, uint64_t flag, entry_scratch *sc,
                        const unsigned char **out_data, size_t *out_len,
                        const char **ext_hint, int *did_decompress, err *fail) {
    *ext_hint = NULL;
    *did_decompress = 0;
    *out_data = raw;
    *out_len = raw_len;

    arena_reset(&sc->scratch);

    if (zp1_looks_like(raw, raw_len)) {
        return 1;
    }

    if (flag == 1) {
        if (!codec_looks_like_split(raw, raw_len, &sc->scratch)) {
            if (codec_read_stored_split(raw, raw_len, orig_size, &sc->work)) {
                *out_data = (const unsigned char *)sc->work.data;
                *out_len = sc->work.len;
                *did_decompress = 1;
                return 1;
            }
        }

        if (codec_looks_like_split(raw, raw_len, &sc->scratch)) {
            err split_err;
            err_clear(&split_err);
            if (codec_looks_like_pairtable(raw, raw_len, &sc->scratch)) {
                *out_data = raw;
                *out_len = raw_len;
                *did_decompress = 0;
                return 1;
            }
            if (codec_decompress_split(raw, raw_len, &sc->scratch, &sc->work, ext_hint, &split_err)) {
                *out_data = (const unsigned char *)sc->work.data;
                *out_len = sc->work.len;
                *did_decompress = 1;
                return 1;
            }
            if (codec_zlib_header_anywhere(raw, raw_len, &sc->work, fail)) {
                *out_data = (const unsigned char *)sc->work.data;
                *out_len = sc->work.len;
                *did_decompress = 1;
                return 1;
            }
            return 0;
        }

        if (codec_zlib_header_anywhere(raw, raw_len, &sc->work, fail)) {
            *out_data = (const unsigned char *)sc->work.data;
            *out_len = sc->work.len;
            *did_decompress = 1;
            return 1;
        }
        return 0;
    }

    if (codec_looks_like_split(raw, raw_len, &sc->scratch)) {
        if (codec_looks_like_pairtable(raw, raw_len, &sc->scratch)) {
            *out_data = raw;
            *out_len = raw_len;
            *did_decompress = 0;
            return 1;
        }
        err split_err;
        err_clear(&split_err);
        if (codec_decompress_split(raw, raw_len, &sc->scratch, &sc->work, ext_hint, &split_err)) {
            *out_data = (const unsigned char *)sc->work.data;
            *out_len = sc->work.len;
            *did_decompress = 1;
            return 1;
        }
        err_set(fail, "%s", split_err.set ? split_err.text : "split-zlib fallback failed");
        return 0;
    }

    return 1;
}

typedef struct {
    int keep;
    uint64_t offset;
    uint64_t size_to_read;
    uint64_t orig_size;
    uint64_t flag;
    int64_t entry_off_abs;
    int64_t file_index;
    int comp_marker_out;
    int64_t unpacked_size;
    const char *ext;
    char name[48];
    char *rel;
    const char *listed;
} slot_plan;

typedef struct {
    const unpack_opts *opts;
    manifest_writer *manifest;
    unpack_stats *stats;
    int64_t total_bytes;
    volatile LONG64 done_bytes;
    int64_t total_entries;
    volatile LONG64 done_entries;
} unpack_run_state;

typedef struct {
    unpack_run_state *st;
    const game_schema *s;
    const container_map *bin;
    const char *bin_name;
    const char *pair_out_dir;
    slot_plan *plans;
    const int64_t *kept;
    int64_t kept_count;
    const name_list *names;
    int idx_marker;
    job_ctx *job;

    volatile LONG cursor;
    volatile LONG failed;
    CRITICAL_SECTION error_lock;
    err first_error;

    volatile LONG64 nested_written;
    volatile LONG64 files_written;
    volatile LONG64 decompress_failures;
} pair_work;

static int execute_entry(pair_work *w, slot_plan *plan, int64_t entry_number,
                         entry_scratch *sc, buf *cipher_buf,
                         int64_t *nested_local, int64_t *files_local, int64_t *fails_local,
                         err *e) {
    const game_schema *s = w->s;
    const unsigned char *raw = w->bin->base + plan->offset;
    size_t raw_len = (size_t)plan->size_to_read;

    int large = plan->size_to_read >= UNPACK_LARGE_ENTRY;
    if (large) {
        EnterCriticalSection(&large_entry_lock);
    }

    int ok_overall = 1;

    if (s->has_cipher && crypt_applies_to(plan->flag)) {
        buf_reset(cipher_buf);
        if (!buf_put(cipher_buf, raw, raw_len)) {
            err_set(e, "out of memory decrypting entry %lld", (long long)entry_number);
            ok_overall = 0;
        } else {
            crypt_transform((unsigned char *)cipher_buf->data, raw_len,
                            plan->entry_off_abs / s->entry_size);
            raw = (const unsigned char *)cipher_buf->data;
        }
    }

    const unsigned char *data = raw;
    size_t data_len = raw_len;
    const char *ext_hint = NULL;
    int did_decompress = 0;
    err fail;
    err_clear(&fail);
    int decoded = 0;

    if (ok_overall) {
        decoded = decode_entry(raw, raw_len, plan->orig_size, plan->flag, sc,
                               &data, &data_len, &ext_hint, &did_decompress, &fail);
        if (!decoded) {
            data = raw;
            data_len = raw_len;
            did_decompress = 0;
            (*fails_local)++;
        }

        plan->ext = codec_resolve_ext(data, data_len, ext_hint);
        plan->comp_marker_out = did_decompress ? 1 : 0;
        plan->unpacked_size = (int64_t)data_len;
        if (plan->rel == NULL) {
            snprintf(plan->name, sizeof(plan->name), "entry_%05lld%s",
                     (long long)plan->file_index, plan->ext);
        }
        const char *written_as = plan->rel != NULL ? plan->rel : plan->name;

        if (w->st->opts->write_files) {
            char *out_path = path_join(w->pair_out_dir, written_as);
            if (out_path == NULL) {
                err_set(e, "out of memory building an output path");
                ok_overall = 0;
            } else {
                if (!file_write_prepared(out_path, data, data_len)) {
                    err_set(e, "couldnt write %s", written_as);
                    ok_overall = 0;
                } else {
                    (*files_local)++;
                    nested_unpack_resource(w->job, out_path, data, data_len, 0, nested_local);
                }
                free(out_path);
            }
        }
    }

    if (large) {
        LeaveCriticalSection(&large_entry_lock);
    }

    if (ok_overall && !decoded) {
        char text[ERR_MAX + 256];
        snprintf(text, sizeof(text),
                 "decompress failed at IDX entry %lld (BIN=%s, offset=0x%llX, size=0x%llX): %s; wrote raw to %s",
                 (long long)entry_number, w->bin_name, (unsigned long long)plan->offset,
                 (unsigned long long)plan->size_to_read, fail.set ? fail.text : "unknown",
                 plan->name);
        log_failure(w->st->opts->state_dir, text);
    }

    return ok_overall;
}

static DWORD WINAPI pair_worker(LPVOID param) {
    pair_work *w = (pair_work *)param;

    entry_scratch sc;
    entry_scratch_init(&sc);
    buf cipher_buf;
    buf_init(&cipher_buf);

    int64_t nested_local = 0;
    int64_t files_local = 0;
    int64_t fails_local = 0;

    for (;;) {
        if (w->failed || job_cancelled(w->job)) {
            break;
        }
        LONG taken = InterlockedIncrement(&w->cursor) - 1;
        if (taken < 0 || (int64_t)taken >= w->kept_count) {
            break;
        }

        int64_t slot = w->kept[taken];
        err local_err;
        err_clear(&local_err);
        if (!execute_entry(w, &w->plans[slot], slot, &sc, &cipher_buf,
                           &nested_local, &files_local, &fails_local, &local_err)) {
            EnterCriticalSection(&w->error_lock);
            if (!w->first_error.set) {
                w->first_error = local_err;
            }
            LeaveCriticalSection(&w->error_lock);
            InterlockedExchange(&w->failed, 1);
            break;
        }

        InterlockedIncrement64(&w->st->done_entries);
        LONG64 done = InterlockedAdd64(&w->st->done_bytes,
                                       (LONG64)w->plans[slot].size_to_read);
        if ((w->st->done_entries & 7) == 0) {
            emit_progress(w->job, done, w->st->total_bytes, w->bin_name);
        }
    }

    InterlockedAdd64(&w->nested_written, nested_local);
    InterlockedAdd64(&w->files_written, files_local);
    InterlockedAdd64(&w->decompress_failures, fails_local);

    buf_free(&cipher_buf);
    entry_scratch_free(&sc);
    nested_thread_cleanup();
    return 0;
}

static int unpack_one_pair(job_ctx *job, unpack_run_state *st, const game_schema *s,
                           int idx_marker, const char *idx_path, const char *bin_path,
                           err *e) {
    buf idx_data;
    buf_init(&idx_data);
    if (!file_read_all(idx_path, &idx_data)) {
        buf_free(&idx_data);
        err_set(e, "couldnt read %s", idx_path);
        return 0;
    }

    int64_t start_from = s->start_from_offset > 0 ? s->start_from_offset : 0;
    if (start_from > 0 && (size_t)start_from >= idx_data.len) {
        buf_free(&idx_data);
        err_set(e, "Start_From_Offset %lld is beyond the IDX size", (long long)start_from);
        return 0;
    }

    const unsigned char *table = (const unsigned char *)idx_data.data + start_from;
    size_t table_len = idx_data.len - (size_t)start_from;
    int64_t total = (int64_t)(table_len / (size_t)s->entry_size);

    container_map bin;
    if (!container_open(&bin, bin_path, e)) {
        buf_free(&idx_data);
        return 0;
    }

    const char *bin_name = strrchr(bin_path, '\\');
    bin_name = bin_name == NULL ? bin_path : bin_name + 1;

    char pair_dir_rel[256];
    snprintf(pair_dir_rel, sizeof(pair_dir_rel), "%s\\Pack_%02d", s->unpack_folder, idx_marker);
    char *pair_out_dir = path_join(st->opts->out_root, pair_dir_rel);
    if (pair_out_dir == NULL) {
        container_close(&bin);
        buf_free(&idx_data);
        err_set(e, "out of memory building the output folder path");
        return 0;
    }
    if (st->opts->write_files && !path_make_dirs(pair_out_dir)) {
        free(pair_out_dir);
        container_close(&bin);
        buf_free(&idx_data);
        err_set(e, "couldnt create the output folder");
        return 0;
    }

    emit_log(job, "info", "%s: %lld IDX entries", bin_name, (long long)total);

    slot_plan *plans = (slot_plan *)calloc((size_t)(total > 0 ? total : 1), sizeof(slot_plan));
    int64_t *kept = (int64_t *)malloc(sizeof(int64_t) * (size_t)(total > 0 ? total : 1));
    if (plans == NULL || kept == NULL) {
        free(plans);
        free(kept);
        free(pair_out_dir);
        container_close(&bin);
        buf_free(&idx_data);
        err_set(e, "out of memory planning %lld entries", (long long)total);
        return 0;
    }

    int64_t kept_count = 0;
    int64_t file_index = 0;
    for (int64_t i = 0; i < total; i++) {
        size_t start = (size_t)i * (size_t)s->entry_size;
        idx_entry ent;
        schema_read_entry(s, table + start, &ent);

        slot_plan *plan = &plans[i];
        plan->keep = 0;
        plan->offset = ent.offset;
        plan->orig_size = ent.orig_size;
        plan->flag = ent.comp_marker;
        plan->entry_off_abs = start_from + (int64_t)start;

        if (ent.orig_size == 0 && ent.comp_size == 0) {
            st->stats->skipped++;
            continue;
        }
        if (ent.comp_size == 0) {
            st->stats->skipped++;
            continue;
        }

        uint64_t size_to_read = ent.orig_size;
        if (ent.comp_size > 0 && ent.comp_marker == 1) {
            size_to_read = ent.comp_size;
        }
        if (size_to_read == 0) {
            st->stats->skipped++;
            continue;
        }
        if (ent.offset + size_to_read > (uint64_t)bin.size) {
            emit_log(job, "warn",
                     "Entry %lld out of range (offset=0x%llX, size=0x%llX) in %s; skipping",
                     (long long)i, (unsigned long long)ent.offset,
                     (unsigned long long)size_to_read, bin_name);
            st->stats->skipped++;
            continue;
        }
        if (ent.orig_size == 0 &&
            codec_looks_like_empty_stub(bin.base + ent.offset, (size_t)size_to_read)) {
            st->stats->skipped++;
            continue;
        }

        plan->keep = 1;
        plan->size_to_read = size_to_read;
        plan->file_index = file_index++;
        kept[kept_count++] = i;
    }

    name_list names;
    int have_names = name_list_load(&names, st->opts->ref_dir, s->game_id, NULL);
    if (have_names) {
        size_t room = (size_t)(kept_count > 0 ? kept_count : 1);
        char **rel = (char **)calloc(room, sizeof(char *));
        int64_t *slot = (int64_t *)malloc(sizeof(int64_t) * room);
        if (rel != NULL && slot != NULL) {
            int64_t got = 0;
            for (int64_t k = 0; k < kept_count; k++) {
                const char *nm = name_at(&names, kept[k]);
                rel[k] = nm != NULL ? path_sanitize_relative(nm) : NULL;
                slot[k] = kept[k];
                got += rel[k] != NULL;
            }
            err quiet;
            err_clear(&quiet);
            if (got > 0 && names_make_unique(rel, slot, kept_count, &quiet)) {
                for (int64_t k = 0; k < kept_count; k++) {
                    plans[kept[k]].rel = rel[k];
                    rel[k] = NULL;
                }
            }
            for (int64_t k = 0; k < kept_count; k++) {
                if (plans[kept[k]].rel != NULL) {
                    plans[kept[k]].listed = name_at(&names, kept[k]);
                }
            }
            for (int64_t k = 0; k < kept_count; k++) {
                free(rel[k]);
            }
            emit_log(job, "info", "%s: %lld of %lld entries carry a name",
                     bin_name, (long long)got, (long long)kept_count);
        }
        free(rel);
        free(slot);
    }

    if (st->opts->write_files) {
        for (int64_t k = 0; k < kept_count; k++) {
            const char *shaped = plans[kept[k]].rel;
            if (shaped == NULL || strchr(shaped, '\\') == NULL) {
                continue;
            }
            char *full = path_join(pair_out_dir, shaped);
            if (full == NULL) {
                continue;
            }
            if (!path_make_parent_dirs(full)) {
                emit_log(job, "warn", "couldnt create the folder for %s", shaped);
            }
            free(full);
        }
    }

    pair_work work;
    memset(&work, 0, sizeof(work));
    work.st = st;
    work.s = s;
    work.bin = &bin;
    work.bin_name = bin_name;
    work.pair_out_dir = pair_out_dir;
    work.plans = plans;
    work.kept = kept;
    work.kept_count = kept_count;
    work.idx_marker = idx_marker;
    work.job = job;
    err_clear(&work.first_error);
    InitializeCriticalSection(&work.error_lock);

    unsigned cores = cpu_count();
    if (cores > UNPACK_MAX_THREADS) {
        cores = UNPACK_MAX_THREADS;
    }
    if (cores == 0) {
        cores = 1;
    }
    if ((int64_t)cores > kept_count) {
        cores = (unsigned)(kept_count > 0 ? kept_count : 1);
    }

    HANDLE threads[UNPACK_MAX_THREADS];
    unsigned started = 0;
    for (unsigned t = 0; t + 1 < cores; t++) {
        HANDLE handle = CreateThread(NULL, 0, pair_worker, &work, 0, NULL);
        if (handle == NULL) {
            break;
        }
        threads[started++] = handle;
    }

    pair_worker(&work);

    for (unsigned t = 0; t < started; t++) {
        WaitForSingleObject(threads[t], INFINITE);
        CloseHandle(threads[t]);
    }

    st->stats->entries_seen += total;
    st->stats->files_written += work.files_written;
    st->stats->nested_written += work.nested_written;
    st->stats->decompress_failures += work.decompress_failures;

    int ok = !work.failed;
    if (!ok) {
        *e = work.first_error;
        if (!e->set) {
            err_set(e, "unpack failed");
        }
    }

    DeleteCriticalSection(&work.error_lock);
    emit_progress(job, st->done_bytes, st->total_bytes, bin_name);

    if (ok) {
        for (int64_t k = 0; k < kept_count; k++) {
            slot_plan *plan = &plans[kept[k]];
            char key[1024];
            char rel_slash[512];
            snprintf(rel_slash, sizeof(rel_slash), "%s",
                     plan->rel != NULL ? plan->rel : plan->name);
            path_to_slash(rel_slash);
            snprintf(key, sizeof(key), "%s/Pack_%02d/%s", s->unpack_folder, idx_marker,
                     rel_slash);
            manifest_record(st->manifest, key, idx_marker, plan->entry_off_abs,
                            plan->comp_marker_out, bin_name, kept[k],
                            plan->unpacked_size, plan->ext == NULL ? ".bin" : plan->ext,
                            plan->listed, 0, 0, NULL, 0.0f, 0);
        }
    }

    if (have_names) {
        name_list_free(&names);
    }
    for (int64_t k = 0; k < kept_count; k++) {
        free(plans[kept[k]].rel);
    }
    free(plans);
    free(kept);
    free(pair_out_dir);
    container_close(&bin);
    buf_free(&idx_data);
    return ok;
}

static int unpack_multi(job_ctx *job, unpack_run_state *st, const game_schema *s,
                        const char *idx_path, char **bin_paths, int bin_count, err *e) {
    buf idx_data;
    buf_init(&idx_data);
    if (!file_read_all(idx_path, &idx_data)) {
        buf_free(&idx_data);
        err_set(e, "couldnt read %s", idx_path);
        return 0;
    }

    int64_t start_from = s->start_from_offset > 0 ? s->start_from_offset : 0;
    if (start_from > 0 && (size_t)start_from >= idx_data.len) {
        buf_free(&idx_data);
        err_set(e, "Start_From_Offset %lld is beyond the IDX size", (long long)start_from);
        return 0;
    }

    const unsigned char *table = (const unsigned char *)idx_data.data + start_from;
    size_t table_len = idx_data.len - (size_t)start_from;
    int64_t total = (int64_t)(table_len / (size_t)s->entry_size);

    int current = 0;
    container_map bin;
    if (!container_open(&bin, bin_paths[0], e)) {
        buf_free(&idx_data);
        return 0;
    }

    entry_scratch sc;
    entry_scratch_init(&sc);
    buf cipher_buf;
    buf_init(&cipher_buf);

    int64_t entries_in_current = 0;
    int64_t file_index = 0;
    int failed = 0;

    char pair_dir_rel[256];
    snprintf(pair_dir_rel, sizeof(pair_dir_rel), "%s\\Pack_%02d", s->unpack_folder, current);
    char *pair_out_dir = path_join(st->opts->out_root, pair_dir_rel);
    if (pair_out_dir != NULL && st->opts->write_files) {
        path_make_dirs(pair_out_dir);
    }

    for (int64_t i = 0; i < total && !failed; i++) {
        if (job_cancelled(job)) {
            err_set(e, "Cancelled");
            failed = 1;
            break;
        }

        size_t start = (size_t)i * (size_t)s->entry_size;
        idx_entry ent;
        schema_read_entry(s, table + start, &ent);

        uint64_t size_to_read = ent.orig_size;
        if (ent.comp_size > 0 && ent.comp_marker == 1) {
            size_to_read = ent.comp_size;
        }

        int advanced = 0;
        if (ent.offset == 0 && entries_in_current > 0 && current + 1 < bin_count) {
            advanced = 1;
        }
        if (!advanced && size_to_read > 0 &&
            ent.offset + size_to_read > (uint64_t)bin.size && current + 1 < bin_count) {
            advanced = 1;
        }

        if (advanced) {
            container_close(&bin);
            current++;
            if (!container_open(&bin, bin_paths[current], e)) {
                failed = 1;
                break;
            }
            entries_in_current = 0;
            file_index = 0;
            free(pair_out_dir);
            snprintf(pair_dir_rel, sizeof(pair_dir_rel), "%s\\Pack_%02d", s->unpack_folder, current);
            pair_out_dir = path_join(st->opts->out_root, pair_dir_rel);
            if (pair_out_dir != NULL && st->opts->write_files) {
                path_make_dirs(pair_out_dir);
            }
        }

        if (pair_out_dir == NULL) {
            err_set(e, "out of memory building the output folder path");
            failed = 1;
            break;
        }

        const char *bin_name = strrchr(bin_paths[current], '\\');
        bin_name = bin_name == NULL ? bin_paths[current] : bin_name + 1;

        entries_in_current++;
        st->stats->entries_seen++;

        if (ent.orig_size == 0 && ent.comp_size == 0) {
            st->stats->skipped++;
            continue;
        }
        if (ent.comp_size == 0 || size_to_read == 0) {
            st->stats->skipped++;
            continue;
        }
        if (ent.offset + size_to_read > (uint64_t)bin.size) {
            emit_log(job, "warn", "Entry %lld out of range in %s; skipping",
                     (long long)i, bin_name);
            st->stats->skipped++;
            continue;
        }
        if (ent.orig_size == 0 &&
            codec_looks_like_empty_stub(bin.base + ent.offset, (size_t)size_to_read)) {
            st->stats->skipped++;
            continue;
        }

        slot_plan plan;
        memset(&plan, 0, sizeof(plan));
        plan.keep = 1;
        plan.offset = ent.offset;
        plan.orig_size = ent.orig_size;
        plan.flag = ent.comp_marker;
        plan.size_to_read = size_to_read;
        plan.entry_off_abs = start_from + (int64_t)start;
        plan.file_index = file_index;

        pair_work solo;
        memset(&solo, 0, sizeof(solo));
        solo.st = st;
        solo.s = s;
        solo.bin = &bin;
        solo.bin_name = bin_name;
        solo.pair_out_dir = pair_out_dir;
        solo.job = job;

        int64_t nested_local = 0;
        int64_t files_local = 0;
        int64_t fails_local = 0;
        if (!execute_entry(&solo, &plan, i, &sc, &cipher_buf,
                           &nested_local, &files_local, &fails_local, e)) {
            failed = 1;
            break;
        }

        st->stats->files_written += files_local;
        st->stats->nested_written += nested_local;
        st->stats->decompress_failures += fails_local;

        char key[512];
        snprintf(key, sizeof(key), "%s/Pack_%02d/%s", s->unpack_folder, current, plan.name);
        manifest_record(st->manifest, key, current, plan.entry_off_abs,
                        plan.comp_marker_out, bin_name, i, plan.unpacked_size,
                        plan.ext == NULL ? ".bin" : plan.ext, NULL, 0, 0,
                        NULL, 0.0f, 0);

        file_index++;
        st->done_entries++;
        st->done_bytes += (LONG64)size_to_read;
        if ((i & 63) == 0 || i + 1 == total) {
            emit_progress(job, st->done_bytes, st->total_bytes, bin_name);
        }
    }

    free(pair_out_dir);
    buf_free(&cipher_buf);
    entry_scratch_free(&sc);
    nested_thread_cleanup();
    container_close(&bin);
    buf_free(&idx_data);
    return !failed;
}

int unpack_run(job_ctx *job, const unpack_opts *opts, unpack_stats *stats, err *e) {
    const game_schema *s = opts->schema;
    memset(stats, 0, sizeof(*stats));

    codec_init();
    codec_set_big_endian(!s->little_endian);
    unpack_locks_init();

    char *idx_paths[SCHEMA_MAX_FILES];
    char *bin_paths[SCHEMA_MAX_FILES];
    int idx_found = 0;
    int bin_found = 0;

    for (int i = 0; i < s->idx_count; i++) {
        idx_paths[i] = path_join(opts->base_dir, s->idx_files[i]);
        if (idx_paths[i] == NULL) {
            for (int k = 0; k < i; k++) {
                free(idx_paths[k]);
            }
            err_set(e, "out of memory building IDX paths");
            return 0;
        }
        if (path_is_file(idx_paths[i])) {
            idx_found++;
        }
    }
    for (int i = 0; i < s->container_count; i++) {
        bin_paths[i] = path_join(opts->base_dir, s->containers[i]);
        if (bin_paths[i] == NULL) {
            for (int k = 0; k < s->idx_count; k++) {
                free(idx_paths[k]);
            }
            for (int k = 0; k < i; k++) {
                free(bin_paths[k]);
            }
            err_set(e, "out of memory building container paths");
            return 0;
        }
        if (path_is_file(bin_paths[i])) {
            bin_found++;
        }
    }

    int result = 0;
    manifest_writer manifest;
    unpack_run_state st;
    memset(&st, 0, sizeof(st));

    if (s->family == SCHEMA_FAMILY_LINKDATA_V2) {
        if (!manifest_begin(&manifest, opts->state_dir, opts->out_root, s->game_id, e)) {
            goto cleanup_paths;
        }
        result = linkv2_run(job, opts, stats, &manifest, e);
        goto cleanup_run;
    }

    if ((idx_found == 0 || bin_found == 0) && s->family == SCHEMA_FAMILY_LEGACY &&
        s->container_count > 0 && s->idx_count > 0) {
        char region_bin[LEGACY_NAME_MAX];
        char region_toc[LEGACY_NAME_MAX];
        if (legacy_region_pair(opts->base_dir, s->containers[0], s->idx_files[0],
                               region_bin, region_toc, sizeof(region_bin))) {
            idx_found = 1;
            bin_found = 1;
        }
    }

    if (idx_found == 0 || bin_found == 0) {
        err_set(e, "no containers found for %s in that folder", s->game_id);
        goto cleanup_paths;
    }

    if (!manifest_begin(&manifest, opts->state_dir, opts->out_root, s->game_id, e)) {
        goto cleanup_paths;
    }

    st.opts = opts;
    st.manifest = &manifest;
    st.stats = stats;

    if (s->family == SCHEMA_FAMILY_LEGACY) {
        for (int i = 0; i < legacy_part_count(s->game_id); i++) {
            manifest_container(&manifest, i, legacy_part_at(s->game_id, i)->container);
        }
        manifest_files_open(&manifest);
        result = legacy_run(job, opts, stats, &manifest, e);
        goto cleanup_run;
    }

    for (int i = 0; i < s->container_count; i++) {
        manifest_container(&manifest, i, s->containers[i]);
    }
    manifest_files_open(&manifest);

    for (int i = 0; i < s->idx_count; i++) {
        if (!path_is_file(idx_paths[i])) {
            continue;
        }
        buf probe;
        buf_init(&probe);
        if (file_read_all(idx_paths[i], &probe)) {
            int64_t start_from = s->start_from_offset > 0 ? s->start_from_offset : 0;
            if ((size_t)start_from < probe.len) {
                const unsigned char *table = (const unsigned char *)probe.data + start_from;
                int64_t slots = (int64_t)((probe.len - (size_t)start_from) / (size_t)s->entry_size);
                st.total_entries += slots;
                for (int64_t k = 0; k < slots; k++) {
                    idx_entry ent;
                    schema_read_entry(s, table + (size_t)k * (size_t)s->entry_size, &ent);
                    if (ent.comp_size == 0) {
                        continue;
                    }
                    uint64_t size_to_read = ent.orig_size;
                    if (ent.comp_marker == 1) {
                        size_to_read = ent.comp_size;
                    }
                    st.total_bytes += (int64_t)size_to_read;
                }
            }
        }
        buf_free(&probe);
    }
    if (st.total_bytes <= 0) {
        st.total_bytes = 1;
    }

    if (s->idx_count == s->container_count) {
        for (int i = 0; i < s->idx_count; i++) {
            if (!path_is_file(idx_paths[i]) || !path_is_file(bin_paths[i])) {
                emit_log(job, "warn", "Skipping pair %d, a file is missing", i);
                continue;
            }
            if (!unpack_one_pair(job, &st, s, i, idx_paths[i], bin_paths[i], e)) {
                goto cleanup_run;
            }
        }
    } else if (s->idx_count == 1) {
        if (!unpack_multi(job, &st, s, idx_paths[0], bin_paths, s->container_count, e)) {
            goto cleanup_run;
        }
    } else {
        err_set(e, "unsupported container layout for %s", s->game_id);
        goto cleanup_run;
    }

    emit_progress(job, st.total_bytes, st.total_bytes, "done");
    result = 1;

cleanup_run:
    if (result) {
        if (!manifest_finish(&manifest)) {
            err_set(e, "couldnt finish writing the taildata manifest");
            result = 0;
        }
    } else {
        manifest_abort(&manifest);
    }

cleanup_paths:
    for (int i = 0; i < s->idx_count; i++) {
        free(idx_paths[i]);
    }
    for (int i = 0; i < s->container_count; i++) {
        free(bin_paths[i]);
    }
    return result;
}
