#define WIN32_LEAN_AND_MEAN
#include "linkv2.h"
#include "codec.h"
#include "names.h"
#include "nested.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LINKV2_MAX_THREADS 16
#define LINKV2_LARGE_ENTRY (64u << 20)
#define LINKV2_ZL_MAX_TOTAL 0x40000000u
#define LINKV2_REF_KEY_MAX 96

static const linkv2_container aot1_containers[] = {
    { "LINK_A", NULL, {"LINKDATA_EU_A.BIN", "LINKDATA_JP_A.BIN", "LINKDATA_AS_A.BIN",
                       "LINKDATA_US_A.BIN", "LINKDATA_CH_A.BIN", "LINKDATA_KR_A.BIN",
                       "LINKDATA_A.BIN"}, 7 },
    { "LINK_B", NULL, {"LINKDATA_EU_B.BIN", "LINKDATA_JP_B.BIN", "LINKDATA_AS_B.BIN",
                       "LINKDATA_US_B.BIN", "LINKDATA_CH_B.BIN", "LINKDATA_KR_B.BIN",
                       "LINKDATA_B.BIN"}, 7 },
    { "LINK_C", NULL, {"LINKDATA_EU_C.BIN", "LINKDATA_JP_C.BIN", "LINKDATA_AS_C.BIN",
                       "LINKDATA_US_C.BIN", "LINKDATA_CH_C.BIN", "LINKDATA_KR_C.BIN",
                       "LINKDATA_C.BIN"}, 7 },
    { "LINK_D", NULL, {"LINKDATA_EU_D.BIN", "LINKDATA_JP_D.BIN", "LINKDATA_AS_D.BIN",
                       "LINKDATA_US_D.BIN", "LINKDATA_CH_D.BIN", "LINKDATA_KR_D.BIN",
                       "LINKDATA_D.BIN"}, 7 },
    { "LINK_PLATFORM", NULL, {"LINKDATA_EU_PLATFORM.BIN", "LINKDATA_JP_PLATFORM.BIN",
                              "LINKDATA_AS_PLATFORM.BIN", "LINKDATA_US_PLATFORM.BIN",
                              "LINKDATA_CH_PLATFORM.BIN", "LINKDATA_KR_PLATFORM.BIN",
                              "LINKDATA_PLATFORM.BIN"}, 7 },
};

static const linkv2_container aot2_containers[] = {
    { "LINK_A", NULL, {"LINKDATA_A.BIN"}, 1 },
    { "LINK_B", NULL, {"LINKDATA_B.BIN"}, 1 },
    { "LINK_C", NULL, {"LINKDATA_C.BIN"}, 1 },
    { "LINK_D", NULL, {"LINKDATA_D.BIN"}, 1 },
    { "LINK_DEBUG", NULL, {"LINKDATA_DEBUG.BIN"}, 1 },
    { "LINK_DLC", NULL, {"LINKDATA_DLC.BIN"}, 1 },
    { "LINK_PLATFORM_DX11", NULL, {"LINKDATA_PLATFORM_DX11.BIN"}, 1 },
    { "LINK_PLATFORM_EDEN", NULL, {"LINKDATA_PLATFORM_EDEN_DX11.BIN"}, 1 },
    { "REGION_JP", NULL, {"REGION\\LINKDATA_REGION_JP.BIN"}, 1 },
    { "REGION_AS", NULL, {"REGION\\LINKDATA_REGION_AS.BIN"}, 1 },
    { "REGION_EDEN_AS", NULL, {"REGION\\LINKDATA_REGION_EDEN_AS.BIN"}, 1 },
    { "REGION_EDEN_EU", NULL, {"REGION\\LINKDATA_REGION_EDEN_EU.BIN"}, 1 },
    { "REGION_EDEN_JP", NULL, {"REGION\\LINKDATA_REGION_EDEN_JP.BIN"}, 1 },
    { "REGION_EU", NULL, {"REGION\\LINKDATA_REGION_EU.BIN"}, 1 },
    { "LINK_EX", NULL, {"EX\\LINKDATA_EX_MASTER.BIN"}, 1 },
    { "LINK_PATCH", NULL, {"PATCH\\LINKDATA_PATCH_000.BIN"}, 1 },
    { "LINK_PATCH_EDEN", NULL, {"PATCH\\LINKDATA_PATCH_EDEN_000.BIN"}, 1 },
    { "DLC_D", "DLC_LINKDATA_D", {"FILE\\DLC\\LINKDATA_D.BIN"}, 1 },
};

static const linkv2_container op3_containers[] = {
    { "LINK_A", NULL, {"LINKDATA_EU_OS_EUNA.A"}, 1 },
    { "LINK_B", NULL, {"LINKDATA_EU_OS_EUNA.B"}, 1 },
    { "LINK_C", NULL, {"LINKDATA_EU_OS_EUNA.C"}, 1 },
    { "LINK_D", NULL, {"LINKDATA_EU_OS_EUNA.D"}, 1 },
};

static const linkv2_game linkv2_games[] = {
    { "AOT1", LINKV2_MAGIC_AOT, 11, LINKV2_SIZE_TOC, {"", "LINKDATA\\"}, 2, aot1_containers,
      (int)(sizeof(aot1_containers) / sizeof(aot1_containers[0])) },
    { "AOT2", LINKV2_MAGIC_AOT, 8, LINKV2_SIZE_TOC, {"", "LINKDATA\\"}, 2, aot2_containers,
      (int)(sizeof(aot2_containers) / sizeof(aot2_containers[0])) },
    { "OP3", LINKV2_MAGIC_OP3, 11, LINKV2_SIZE_TOC, {"", "LINKDATA_DX9\\"}, 2, op3_containers,
      (int)(sizeof(op3_containers) / sizeof(op3_containers[0])) },
};
const linkv2_game *linkv2_game_for(const char *game_id) {
    if (game_id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(linkv2_games) / sizeof(linkv2_games[0]); i++) {
        if (strcmp(linkv2_games[i].game_id, game_id) == 0) {
            return &linkv2_games[i];
        }
    }
    return NULL;
}

int linkv2_container_count(const char *game_id) {
    const linkv2_game *game = linkv2_game_for(game_id);
    return game == NULL ? 0 : game->container_count;
}

const linkv2_container *linkv2_container_at(const char *game_id, int index) {
    const linkv2_game *game = linkv2_game_for(game_id);
    if (game == NULL || index < 0 || index >= game->container_count) {
        return NULL;
    }
    return &game->containers[index];
}

int linkv2_is_magic(uint32_t magic) {
    return magic == LINKV2_MAGIC_AOT || magic == LINKV2_MAGIC_OP3;
}

static void parse_header(const unsigned char *raw, linkv2_header *out) {
    out->magic = codec_u32(raw, 0);
    out->count = codec_u32(raw, 4);
    out->alignment = codec_u32(raw, 8);
}

int linkv2_read_header(const char *path, linkv2_header *out) {
    FILE *handle = file_open(path, "rb");
    if (handle == NULL) {
        return 0;
    }
    unsigned char raw[LINKV2_HEADER_SIZE];
    size_t got = fread(raw, 1, sizeof(raw), handle);
    fclose(handle);
    if (got != sizeof(raw)) {
        return 0;
    }
    parse_header(raw, out);
    return linkv2_is_magic(out->magic);
}

int linkv2_path_count(const linkv2_game *game, const linkv2_container *c) {
    int roots = game->root_count > 0 ? game->root_count : 1;
    return c->candidate_count * roots;
}

int linkv2_path_at(const linkv2_game *game, const linkv2_container *c, int index,
                   char *out, size_t room) {
    int roots = game->root_count > 0 ? game->root_count : 1;
    if (index < 0 || index >= linkv2_path_count(game, c)) {
        return 0;
    }
    const char *root = game->root_count > 0 ? game->roots[index % roots] : "";
    const char *candidate = c->candidates[index / roots];
    return snprintf(out, room, "%s%s", root, candidate) < (int)room;
}

int linkv2_resolve(const char *base_dir, const linkv2_game *game, const linkv2_container *c,
                   char *out, size_t room) {
    char rel[LINKV2_NAME_MAX];
    int fallback = -1;
    for (int i = 0; i < linkv2_path_count(game, c); i++) {
        if (!linkv2_path_at(game, c, i, rel, sizeof(rel))) {
            continue;
        }
        char *path = path_join(base_dir, rel);
        if (path == NULL) {
            return 0;
        }
        int present = path_is_file(path);
        linkv2_header header;
        int real = present && linkv2_read_header(path, &header);
        free(path);
        if (real) {
            snprintf(out, room, "%s", rel);
            return 1;
        }
        if (present && fallback < 0) {
            fallback = i;
        }
    }
    if (fallback < 0) {
        return 0;
    }
    return linkv2_path_at(game, c, fallback, out, room);
}

static int zl_header_at(const unsigned char *data, size_t len, size_t off) {
    if (off + 2 > len) {
        return 0;
    }
    unsigned cmf = data[off];
    unsigned flg = data[off + 1];
    if ((cmf & 0x0F) != 8 || (cmf >> 4) > 7) {
        return 0;
    }
    return ((cmf << 8) + flg) % 31 == 0;
}

int linkv2_zl_looks_like(const unsigned char *data, size_t len) {
    if (len < 12) {
        return 0;
    }
    uint32_t total = codec_u32(data, 0);
    uint32_t first = codec_u32(data, 4);
    return total > 0 && total <= LINKV2_ZL_MAX_TOTAL && first > 0 &&
           (size_t)first <= len - 8 && zl_header_at(data, len, 8);
}

int linkv2_zl_decompress(const unsigned char *data, size_t len, buf *out, err *e) {
    buf_reset(out);
    if (len < 8) {
        err_set(e, "ZL buffer too small");
        return 0;
    }

    uint32_t total = codec_u32(data, 0);
    uint32_t chunk_size = codec_u32(data, 4);
    size_t at = 8;
    unsigned chunk = 0;

    buf piece;
    buf_init(&piece);
    int ok = 1;

    while (out->len < total) {
        if (chunk_size == 0) {
            err_set(e, "ZL chunk %u has no size", chunk);
            ok = 0;
            break;
        }
        if (at + chunk_size > len) {
            err_set(e, "ZL chunk %u runs past the entry", chunk);
            ok = 0;
            break;
        }
        if (!zl_header_at(data, len, at)) {
            break;
        }
        if (!codec_inflate(data + at, chunk_size, &piece, e)) {
            ok = 0;
            break;
        }
        if (!buf_put(out, piece.data, piece.len)) {
            err_set(e, "out of memory joining ZL chunks");
            ok = 0;
            break;
        }
        at += chunk_size;
        chunk++;
        if (out->len >= total || at + 4 > len) {
            break;
        }
        chunk_size = codec_u32(data, at);
        at += 4;
    }

    buf_free(&piece);
    if (ok && out->len < total) {
        err_set(e, "ZL came out short, %zu of %u bytes", out->len, total);
        ok = 0;
    }
    if (ok) {
        out->len = total;
    }
    return ok;
}

typedef struct {
    HANDLE file;
    HANDLE mapping;
    const unsigned char *base;
    int64_t size;
} linkv2_map;

static void map_close(linkv2_map *m) {
    if (m->base != NULL) {
        UnmapViewOfFile((LPCVOID)m->base);
    }
    if (m->mapping != NULL && m->mapping != INVALID_HANDLE_VALUE) {
        CloseHandle(m->mapping);
    }
    if (m->file != NULL && m->file != INVALID_HANDLE_VALUE) {
        CloseHandle(m->file);
    }
    memset(m, 0, sizeof(*m));
}

static int map_open(linkv2_map *m, const char *path, err *e) {
    memset(m, 0, sizeof(*m));
    wchar_t *wide = path_to_wide(path);
    if (wide == NULL) {
        err_set(e, "out of memory opening %s", path);
        return 0;
    }
    m->file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wide);
    if (m->file == INVALID_HANDLE_VALUE) {
        err_set(e, "couldnt open %s", path);
        return 0;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(m->file, &size) || size.QuadPart < LINKV2_HEADER_SIZE) {
        map_close(m);
        err_set(e, "%s is too small to hold a LINKDATA header", path);
        return 0;
    }
    m->size = (int64_t)size.QuadPart;
    m->mapping = CreateFileMappingW(m->file, NULL, PAGE_READONLY, 0, 0, NULL);
    if (m->mapping == NULL) {
        map_close(m);
        err_set(e, "couldnt map %s", path);
        return 0;
    }
    m->base = (const unsigned char *)MapViewOfFile(m->mapping, FILE_MAP_READ, 0, 0, 0);
    if (m->base == NULL) {
        map_close(m);
        err_set(e, "couldnt view %s", path);
        return 0;
    }
    return 1;
}

typedef struct {
    int64_t slot_index;
    int64_t offset;
    int64_t size;
    int64_t toc_off;
    const char *listed;
    char *rel;
    const char *ext;
    int zl;
    int decompressed;
    int64_t unpacked_size;
} linkv2_plan;

typedef struct {
    job_ctx *job;
    const unpack_opts *opts;
    const unsigned char *base;
    const char *bin_name;
    const char *pack_dir;
    linkv2_plan *plans;
    int64_t count;

    volatile LONG cursor;
    volatile LONG failed;
    CRITICAL_SECTION error_lock;
    err first_error;

    volatile LONG64 files_written;
    volatile LONG64 nested_written;
    volatile LONG64 decompress_failures;

    int64_t total_bytes;
    volatile LONG64 *done_bytes;
} linkv2_work;

static CRITICAL_SECTION large_entry_lock;
static CRITICAL_SECTION failure_log_lock;
static int locks_ready;

static void locks_init(void) {
    if (!locks_ready) {
        InitializeCriticalSection(&large_entry_lock);
        InitializeCriticalSection(&failure_log_lock);
        locks_ready = 1;
    }
}

static void log_failure(const char *state_dir, const char *text) {
    char *path = path_join(state_dir, "gokon_decompress_failures.log");
    if (path == NULL) {
        return;
    }
    EnterCriticalSection(&failure_log_lock);
    FILE *handle = file_open(path, "ab");
    if (handle != NULL) {
        fputs(text, handle);
        fputc('\n', handle);
        fclose(handle);
    }
    LeaveCriticalSection(&failure_log_lock);
    free(path);
}

static void work_fail(linkv2_work *w, const char *text) {
    EnterCriticalSection(&w->error_lock);
    if (!w->first_error.set) {
        err_set(&w->first_error, "%s", text);
    }
    LeaveCriticalSection(&w->error_lock);
    InterlockedExchange(&w->failed, 1);
}

static DWORD WINAPI plan_worker(LPVOID param) {
    linkv2_work *w = (linkv2_work *)param;
    int64_t files_local = 0;
    int64_t nested_local = 0;
    int64_t fails_local = 0;
    buf plain;
    buf_init(&plain);

    for (;;) {
        if (w->failed || job_cancelled(w->job)) {
            break;
        }
        LONG taken = InterlockedIncrement(&w->cursor) - 1;
        if (taken < 0 || (int64_t)taken >= w->count) {
            break;
        }

        linkv2_plan *plan = &w->plans[taken];
        const unsigned char *data = w->base + plan->offset;
        size_t len = (size_t)plan->size;

        int large = plan->size >= (int64_t)LINKV2_LARGE_ENTRY;
        if (large) {
            EnterCriticalSection(&large_entry_lock);
        }

        if (plan->zl) {
            err fail;
            err_clear(&fail);
            if (linkv2_zl_decompress(data, len, &plain, &fail)) {
                data = (const unsigned char *)plain.data;
                len = plain.len;
                plan->decompressed = 1;
            } else {
                fails_local++;
                char text[ERR_MAX + 256];
                snprintf(text, sizeof(text),
                         "ZL decompress failed for %s entry %lld (offset=0x%llX, size=0x%llX): %s; wrote it as stored",
                         w->bin_name, (long long)plan->slot_index,
                         (unsigned long long)plan->offset, (unsigned long long)plan->size,
                         fail.set ? fail.text : "unknown");
                log_failure(w->opts->state_dir, text);
            }
        }

        plan->ext = codec_resolve_ext(data, len, NULL);
        plan->unpacked_size = (int64_t)len;

        int ok = 1;
        if (plan->rel == NULL) {
            char name[64];
            snprintf(name, sizeof(name), "entry_%05lld%s", (long long)plan->slot_index, plan->ext);
            plan->rel = _strdup(name);
            if (plan->rel == NULL) {
                work_fail(w, "out of memory naming an entry");
                ok = 0;
            }
        }

        if (ok && w->opts->write_files) {
            char *path = path_join(w->pack_dir, plan->rel);
            if (path == NULL) {
                work_fail(w, "out of memory building an output path");
                ok = 0;
            } else {
                if (file_write_prepared(path, data, len)) {
                    files_local++;
                    nested_unpack_resource(w->job, path, data, len, 0, &nested_local);
                } else {
                    char text[ERR_MAX];
                    snprintf(text, sizeof(text), "couldnt write %s", plan->rel);
                    work_fail(w, text);
                    ok = 0;
                }
                free(path);
            }
        }

        if (large) {
            LeaveCriticalSection(&large_entry_lock);
        }
        if (!ok) {
            break;
        }

        LONG64 done = InterlockedAdd64(w->done_bytes, (LONG64)plan->size);
        if ((taken & 63) == 0) {
            emit_progress(w->job, done, w->total_bytes, w->bin_name);
        }
    }

    buf_free(&plain);
    InterlockedAdd64(&w->files_written, files_local);
    InterlockedAdd64(&w->nested_written, nested_local);
    InterlockedAdd64(&w->decompress_failures, fails_local);
    nested_thread_cleanup();
    return 0;
}

typedef struct {
    int64_t slot_index;
    int64_t offset;
    uint32_t stored;
    uint32_t decompressed;
} toc_row;

static int compare_rows(const void *left, const void *right) {
    const toc_row *a = (const toc_row *)left;
    const toc_row *b = (const toc_row *)right;
    if (a->offset != b->offset) {
        return a->offset < b->offset ? -1 : 1;
    }
    if (a->slot_index != b->slot_index) {
        return a->slot_index < b->slot_index ? -1 : 1;
    }
    return 0;
}

static void ref_key_for(const char *rel_path, char out[LINKV2_REF_KEY_MAX]) {
    const char *slash = strrchr(rel_path, '\\');
    const char *alt = strrchr(rel_path, '/');
    if (alt != NULL && (slash == NULL || alt > slash)) {
        slash = alt;
    }
    const char *base = slash == NULL ? rel_path : slash + 1;
    size_t len = strlen(base);
    if (len > 4 && _stricmp(base + len - 4, ".BIN") == 0) {
        len -= 4;
    }
    if (len >= LINKV2_REF_KEY_MAX) {
        len = LINKV2_REF_KEY_MAX - 1;
    }
    for (size_t i = 0; i < len; i++) {
        out[i] = base[i] == '.' ? '_' : base[i];
    }
    out[len] = 0;
}

static char *strip_zl_suffix(const char *name) {
    size_t len = strlen(name);
    char *copy = _strdup(name);
    if (copy != NULL && len > 4 && _stricmp(copy + len - 4, ".ZL_") == 0) {
        copy[len - 4] = 0;
    }
    return copy;
}

static void free_plans(linkv2_plan *plans, int64_t count) {
    for (int64_t i = 0; i < count; i++) {
        free(plans[i].rel);
    }
    free(plans);
}

static int plan_container(job_ctx *job, const linkv2_game *game, const linkv2_map *bin,
                          const char *bin_name, const linkv2_header *header,
                          const name_list *names, linkv2_plan **plans_out,
                          int64_t *count_out, err *e) {
    uint64_t alignment = (uint64_t)1 << game->shift_bits;
    if (header->alignment != 0 && header->alignment != alignment) {
        emit_log(job, "warn", "%s declares alignment 0x%X but %s expects 0x%llX; using 0x%llX",
                 bin_name, header->alignment, game->game_id,
                 (unsigned long long)alignment, (unsigned long long)alignment);
    }

    toc_row *rows = (toc_row *)malloc(sizeof(toc_row) * (size_t)(header->count > 0 ? header->count : 1));
    if (rows == NULL) {
        err_set(e, "out of memory reading the %s table", bin_name);
        return 0;
    }

    int64_t valid = 0;
    for (uint32_t i = 0; i < header->count; i++) {
        const unsigned char *raw = bin->base + LINKV2_HEADER_SIZE + (size_t)i * LINKV2_ENTRY_SIZE;
        uint32_t base = codec_u32(raw, 0);
        if (base == 0) {
            continue;
        }
        rows[valid].slot_index = i;
        rows[valid].offset = (int64_t)((uint64_t)base << game->shift_bits);
        rows[valid].stored = codec_u32(raw, 8);
        rows[valid].decompressed = codec_u32(raw, 12);
        valid++;
    }
    qsort(rows, (size_t)valid, sizeof(toc_row), compare_rows);

    linkv2_plan *plans = (linkv2_plan *)calloc((size_t)(valid > 0 ? valid : 1), sizeof(linkv2_plan));
    if (plans == NULL) {
        free(rows);
        err_set(e, "out of memory planning %lld entries", (long long)valid);
        return 0;
    }

    int64_t kept = 0;
    int64_t empty_tail = 0;
    int64_t past_end = 0;
    for (int64_t i = 0; i < valid; i++) {
        int64_t offset = rows[i].offset;
        if (offset >= bin->size) {
            if (rows[i].stored == 0) {
                empty_tail++;
            } else {
                past_end++;
            }
            continue;
        }
        int64_t next = i + 1;
        while (next < valid && rows[next].offset == offset) {
            next++;
        }
        int64_t limit = next < valid ? rows[next].offset : bin->size;
        if (limit > bin->size) {
            limit = bin->size;
        }
        int64_t gap = limit - offset;
        int64_t stored = (int64_t)rows[i].stored;

        int64_t size = gap;
        if (game->size_rule == LINKV2_SIZE_TOC && stored > 0 && stored <= gap) {
            size = stored;
        }
        if (size <= 0) {
            size = stored;
        }
        if (size <= 0 || offset + size > bin->size) {
            continue;
        }

        linkv2_plan *plan = &plans[kept++];
        plan->slot_index = rows[i].slot_index;
        plan->offset = offset;
        plan->size = size;
        plan->toc_off = LINKV2_HEADER_SIZE + rows[i].slot_index * LINKV2_ENTRY_SIZE;
        plan->listed = name_at(names, rows[i].slot_index);
        plan->zl = rows[i].decompressed != 0 &&
                   codec_u32(bin->base + offset, 0) == rows[i].decompressed &&
                   linkv2_zl_looks_like(bin->base + offset, (size_t)size);
        if (plan->listed != NULL) {
            char *trimmed = strip_zl_suffix(plan->listed);
            if (trimmed == NULL) {
                free(rows);
                free_plans(plans, kept);
                err_set(e, "out of memory naming %s entries", bin_name);
                return 0;
            }
            plan->rel = path_sanitize_relative(trimmed);
            free(trimmed);
            if (plan->rel == NULL) {
                free(rows);
                free_plans(plans, kept);
                err_set(e, "out of memory naming %s entries", bin_name);
                return 0;
            }
        }
    }
    free(rows);
    if (empty_tail > 0) {
        emit_log(job, "info", "%s: %lld empty slots point at the end of the container, nothing to unpack",
                 bin_name, (long long)empty_tail);
    }
    if (past_end > 0) {
        emit_log(job, "warn", "%s: %lld entries point past the end of the file, their data isnt in it, skipped",
                 bin_name, (long long)past_end);
    }

    *plans_out = plans;
    *count_out = kept;
    return 1;
}

static int make_names_unique(linkv2_plan *plans, int64_t count, err *e) {
    size_t room = (size_t)(count > 0 ? count : 1);
    char **rel = (char **)malloc(sizeof(char *) * room);
    int64_t *slot = (int64_t *)malloc(sizeof(int64_t) * room);
    if (rel == NULL || slot == NULL) {
        free(rel);
        free(slot);
        err_set(e, "out of memory checking for repeated names");
        return 0;
    }
    for (int64_t i = 0; i < count; i++) {
        rel[i] = plans[i].rel;
        slot[i] = plans[i].slot_index;
    }
    int ok = names_make_unique(rel, slot, count, e);
    for (int64_t i = 0; i < count; i++) {
        plans[i].rel = rel[i];
    }
    free(rel);
    free(slot);
    return ok;
}

static int prepare_folders(const char *pack_dir, const linkv2_plan *plans, int64_t count, err *e) {
    if (!path_make_dirs(pack_dir)) {
        err_set(e, "couldnt create %s", pack_dir);
        return 0;
    }
    for (int64_t i = 0; i < count; i++) {
        if (plans[i].rel == NULL || strchr(plans[i].rel, '\\') == NULL) {
            continue;
        }
        char *full = path_join(pack_dir, plans[i].rel);
        if (full == NULL) {
            err_set(e, "out of memory creating an output folder");
            return 0;
        }
        int ok = path_make_parent_dirs(full);
        free(full);
        if (!ok) {
            err_set(e, "couldnt create the folder for %s", plans[i].rel);
            return 0;
        }
    }
    return 1;
}

static int run_container(job_ctx *job, const unpack_opts *opts, const linkv2_game *game,
                         int idx_marker, const char *rel_path, manifest_writer *manifest,
                         unpack_stats *stats, int64_t total_bytes, volatile LONG64 *done_bytes,
                         err *e) {
    const linkv2_container *c = &game->containers[idx_marker];
    char *bin_path = path_join(opts->base_dir, rel_path);
    if (bin_path == NULL) {
        err_set(e, "out of memory building a container path");
        return 0;
    }

    linkv2_map bin;
    if (!map_open(&bin, bin_path, e)) {
        free(bin_path);
        return 0;
    }
    free(bin_path);

    const char *bin_name = strrchr(rel_path, '\\');
    bin_name = bin_name == NULL ? rel_path : bin_name + 1;

    linkv2_header header;
    parse_header(bin.base, &header);
    if (!linkv2_is_magic(header.magic)) {
        map_close(&bin);
        err_set(e, "%s isnt a LINKDATA container (magic 0x%08X)", bin_name, header.magic);
        return 0;
    }
    if (header.magic != game->magic) {
        emit_log(job, "warn", "%s carries magic 0x%08X, %s normally uses 0x%08X",
                 bin_name, header.magic, game->game_id, game->magic);
    }
    uint64_t toc_end = LINKV2_HEADER_SIZE + (uint64_t)header.count * LINKV2_ENTRY_SIZE;
    if (toc_end > (uint64_t)bin.size) {
        map_close(&bin);
        err_set(e, "%s ended inside its table of contents", bin_name);
        return 0;
    }

    char ref_key[LINKV2_REF_KEY_MAX];
    if (c->ref_key != NULL) {
        snprintf(ref_key, sizeof(ref_key), "%s", c->ref_key);
    } else {
        ref_key_for(rel_path, ref_key);
    }
    name_list names;
    int have_names = name_list_load(&names, opts->ref_dir, game->game_id, ref_key);

    linkv2_plan *plans = NULL;
    int64_t count = 0;
    int ok = plan_container(job, game, &bin, bin_name, &header,
                            have_names ? &names : NULL, &plans, &count, e);

    int64_t named = 0;
    if (ok) {
        for (int64_t i = 0; i < count; i++) {
            named += plans[i].rel != NULL;
        }
        if (named > 0) {
            ok = make_names_unique(plans, count, e);
            emit_log(job, "info", "%s: %lld of %lld entries carry a name",
                     bin_name, (long long)named, (long long)count);
        }
    }

    char pack_rel[256];
    char *pack_dir = NULL;
    if (ok) {
        snprintf(pack_rel, sizeof(pack_rel), "%s\\%s", opts->schema->unpack_folder, c->pack);
        pack_dir = path_join(opts->out_root, pack_rel);
        if (pack_dir == NULL) {
            err_set(e, "out of memory building the output folder path");
            ok = 0;
        }
    }
    if (ok && opts->write_files) {
        ok = prepare_folders(pack_dir, plans, count, e);
    }

    if (ok) {
        emit_log(job, "info", "%s: %u table entries, %lld to unpack",
                 bin_name, header.count, (long long)count);

        linkv2_work work;
        memset(&work, 0, sizeof(work));
        work.job = job;
        work.opts = opts;
        work.base = bin.base;
        work.bin_name = bin_name;
        work.pack_dir = pack_dir;
        work.plans = plans;
        work.count = count;
        work.total_bytes = total_bytes;
        work.done_bytes = done_bytes;
        err_clear(&work.first_error);
        InitializeCriticalSection(&work.error_lock);

        unsigned cores = cpu_count();
        if (cores > LINKV2_MAX_THREADS) {
            cores = LINKV2_MAX_THREADS;
        }
        if (cores == 0) {
            cores = 1;
        }
        if ((int64_t)cores > count) {
            cores = (unsigned)(count > 0 ? count : 1);
        }

        HANDLE threads[LINKV2_MAX_THREADS];
        unsigned started = 0;
        for (unsigned t = 0; t + 1 < cores; t++) {
            HANDLE handle = CreateThread(NULL, 0, plan_worker, &work, 0, NULL);
            if (handle == NULL) {
                break;
            }
            threads[started++] = handle;
        }
        plan_worker(&work);
        for (unsigned t = 0; t < started; t++) {
            WaitForSingleObject(threads[t], INFINITE);
            CloseHandle(threads[t]);
        }

        if (work.failed) {
            *e = work.first_error;
            if (!e->set) {
                err_set(e, "unpack failed in %s", bin_name);
            }
            ok = 0;
        } else if (job_cancelled(job)) {
            err_set(e, "Cancelled");
            ok = 0;
        }
        DeleteCriticalSection(&work.error_lock);

        stats->entries_seen += header.count;
        stats->skipped += (int64_t)header.count - count;
        stats->files_written += work.files_written;
        stats->nested_written += work.nested_written;
        stats->decompress_failures += work.decompress_failures;
    }

    if (ok) {
        for (int64_t i = 0; i < count; i++) {
            linkv2_plan *plan = &plans[i];
            char rel_slash[512];
            char key[768];
            snprintf(rel_slash, sizeof(rel_slash), "%s", plan->rel);
            path_to_slash(rel_slash);
            snprintf(key, sizeof(key), "%s/%s/%s", opts->schema->unpack_folder, c->pack, rel_slash);
            manifest_record(manifest, key, idx_marker, plan->toc_off, plan->decompressed,
                            bin_name, plan->slot_index, plan->unpacked_size,
                            plan->ext == NULL ? ".bin" : plan->ext, plan->listed,
                            0, 0, NULL, 0.0f, 0);
        }
    }

    if (have_names) {
        name_list_free(&names);
    }
    free(pack_dir);
    free_plans(plans, count);
    map_close(&bin);
    return ok;
}

int linkv2_run(job_ctx *job, const unpack_opts *opts, unpack_stats *stats,
               manifest_writer *manifest, err *e) {
    const linkv2_game *game = linkv2_game_for(opts->schema->game_id);
    if (game == NULL) {
        err_set(e, "%s has no linkdata v2 description", opts->schema->game_id);
        return 0;
    }
    locks_init();

    char (*resolved)[LINKV2_NAME_MAX] = calloc((size_t)game->container_count, LINKV2_NAME_MAX);
    int *present = (int *)calloc((size_t)game->container_count, sizeof(int));
    if (resolved == NULL || present == NULL) {
        free(resolved);
        free(present);
        err_set(e, "out of memory resolving containers");
        return 0;
    }

    int64_t total_bytes = 0;
    int found = 0;
    for (int i = 0; i < game->container_count; i++) {
        if (!linkv2_resolve(opts->base_dir, game, &game->containers[i], resolved[i], LINKV2_NAME_MAX)) {
            continue;
        }
        char *path = path_join(opts->base_dir, resolved[i]);
        int64_t size = path == NULL ? -1 : path_file_size(path);
        free(path);
        if (size > 0) {
            present[i] = 1;
            total_bytes += size;
            found++;
        }
    }

    if (found == 0) {
        free(resolved);
        free(present);
        err_set(e, "no containers found for %s in that folder", game->game_id);
        return 0;
    }

    for (int i = 0; i < game->container_count; i++) {
        manifest_container(manifest, i, present[i] ? resolved[i] : game->containers[i].candidates[0]);
    }
    manifest_files_open(manifest);

    volatile LONG64 done_bytes = 0;
    int ok = 1;
    for (int i = 0; i < game->container_count && ok; i++) {
        if (!present[i]) {
            emit_log(job, "warn", "%s isnt in that folder, skipping it",
                     game->containers[i].candidates[0]);
            continue;
        }
        if (job_cancelled(job)) {
            err_set(e, "Cancelled");
            ok = 0;
            break;
        }
        ok = run_container(job, opts, game, i, resolved[i], manifest, stats,
                           total_bytes, &done_bytes, e);
    }

    if (ok) {
        emit_progress(job, total_bytes, total_bytes, "done");
    }
    free(resolved);
    free(present);
    return ok;
}
