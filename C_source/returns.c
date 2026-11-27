#define WIN32_LEAN_AND_MEAN
#include "returns.h"
#include "gsfs.h"
#include "gsjson.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t linkdata_v2_magics[] = {0x00077DF9u, 0x00011E54u};

static int is_v2_magic(uint32_t magic) {
    return magic == linkdata_v2_magics[0] || magic == linkdata_v2_magics[1];
}

static void hex_encode(const unsigned char *data, size_t len, char *out) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 15];
    }
    out[len * 2] = 0;
}

static int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static int is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v' || ch == '\f';
}

static int hex_decode(const char *text, buf *out, char *why, size_t room) {
    buf_reset(out);
    size_t i = 0;
    size_t len = strlen(text);
    while (i < len) {
        if (is_space(text[i])) {
            i++;
            continue;
        }
        int hi = hex_value(text[i]);
        int lo = i + 1 < len ? hex_value(text[i + 1]) : -1;
        if (hi < 0 || lo < 0) {
            size_t at = hi < 0 ? i : i + 1;
            snprintf(why, room, "non-hexadecimal number found in fromhex() arg at position %zu", at);
            return 0;
        }
        char byte = (char)((hi << 4) | lo);
        if (!buf_put(out, &byte, 1)) {
            snprintf(why, room, "out of memory");
            return 0;
        }
        i += 2;
    }
    return 1;
}

static void os_reason(char *out, size_t room) {
    DWORD code = GetLastError();
    char text[256];
    text[0] = 0;
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, text,
                   sizeof(text), NULL);
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r' || text[len - 1] == ' ')) {
        text[--len] = 0;
    }
    snprintf(out, room, "[WinError %lu] %s", (unsigned long)code, text);
}

static int py_index(int64_t index, int count, int *out) {
    if (index < 0) {
        index += count;
    }
    if (index < 0 || index >= count) {
        return 0;
    }
    *out = (int)index;
    return 1;
}

int returns_has_candidates(const gs_part *part) {
    return part != NULL && part->candidate_count > 0;
}

int returns_read_linkdata_header(const char *path, uint32_t *magic, uint32_t *count) {
    FILE *handle = file_open(path, "rb");
    if (handle == NULL) {
        return 0;
    }
    unsigned char raw[RETURNS_HEADER_WIDTH];
    size_t got = fread(raw, 1, sizeof(raw), handle);
    fclose(handle);
    if (got < RETURNS_HEADER_WIDTH) {
        return 0;
    }
    *magic = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16) |
             ((uint32_t)raw[3] << 24);
    *count = (uint32_t)raw[4] | ((uint32_t)raw[5] << 8) | ((uint32_t)raw[6] << 16) |
             ((uint32_t)raw[7] << 24);
    return 1;
}

char *returns_resolve_candidate(const char *game_dir, const gs_part *part) {
    int first_present = -1;
    for (int i = 0; i < part->candidate_count; i++) {
        char *path = path_join(game_dir, part->candidates[i]);
        if (path == NULL) {
            return NULL;
        }
        if (path_is_file(path)) {
            uint32_t magic = 0;
            uint32_t count = 0;
            if (returns_read_linkdata_header(path, &magic, &count) && is_v2_magic(magic)) {
                return path;
            }
            if (first_present < 0) {
                first_present = i;
            }
        }
        free(path);
    }
    return path_join(game_dir, part->candidates[first_present >= 0 ? first_present : 0]);
}

char *returns_container_file(const gs_game *g, const char *game_dir, int64_t idx_marker,
                             const char *name) {
    const gs_part *part = gamedesc_part(g, idx_marker);
    if (returns_has_candidates(part)) {
        return returns_resolve_candidate(game_dir, part);
    }
    return path_join(game_dir, name);
}

static int resolve_pair(const char *game_dir, char *bin_path, char *idx_path, char **bin_out,
                        char **idx_out) {
    *bin_out = bin_path;
    *idx_out = idx_path;
    if (path_is_file(bin_path) || !gs_wants_region_pair(gs_path_name(bin_path))) {
        return 1;
    }
    char *bin_name = NULL;
    char *toc_name = NULL;
    if (!gs_region_pair(game_dir, &bin_name, &toc_name)) {
        return 1;
    }
    char *bin_full = path_join(game_dir, bin_name);
    char *toc_full = path_join(game_dir, toc_name);
    free(bin_name);
    free(toc_name);
    if (bin_full == NULL || toc_full == NULL) {
        free(bin_full);
        free(toc_full);
        return 1;
    }
    free(bin_path);
    free(idx_path);
    *bin_out = bin_full;
    *idx_out = toc_full;
    return 1;
}

int returns_container_for(const gs_game *g, const char *game_dir, int64_t idx_marker,
                          char **bin_out, char **idx_out, err *e) {
    *bin_out = NULL;
    *idx_out = NULL;
    const gs_part *part = gamedesc_part(g, idx_marker);
    if (returns_has_candidates(part)) {
        char *path = returns_resolve_candidate(game_dir, part);
        char *copy = path == NULL ? NULL : _strdup(path);
        if (path == NULL || copy == NULL) {
            free(path);
            err_set(e, "out of memory");
            return 0;
        }
        *bin_out = path;
        *idx_out = copy;
        return 1;
    }
    char *bin_path = NULL;
    char *idx_path = NULL;
    if (part != NULL) {
        const char *toc = part->toc[0] != 0 ? part->toc : part->container;
        bin_path = path_join(game_dir, part->container);
        idx_path = path_join(game_dir, toc);
    } else {
        int bin_at = 0;
        int idx_at = 0;
        if (!py_index(idx_marker, g->container_count, &bin_at) ||
            !py_index(idx_marker, g->idx_count, &idx_at)) {
            err_set(e, "%s has no container %lld.", g->game_id, (long long)idx_marker);
            return 0;
        }
        bin_path = path_join(game_dir, g->containers[bin_at]);
        idx_path = path_join(game_dir, g->idx_files[idx_at]);
    }
    if (bin_path == NULL || idx_path == NULL) {
        free(bin_path);
        free(idx_path);
        err_set(e, "out of memory");
        return 0;
    }
    return resolve_pair(game_dir, bin_path, idx_path, bin_out, idx_out);
}

int returns_check_pourable(const gs_game *g, int64_t idx_marker, err *e) {
    const gs_part *part = gamedesc_part(g, idx_marker);
    if (part != NULL) {
        if (!part->patchable) {
            err_set(e, "%s has no table to repoint, so a mod cant be poured into it.",
                    gs_path_name(part->container));
            return 0;
        }
        return 1;
    }
    if (g->family != 0) {
        err_set(e, "%s has no container %lld this writer knows how to pour into.",
                g->display_name, (long long)idx_marker);
        return 0;
    }
    return 1;
}

static int has_name(const char *const *names, int count, const char *name) {
    for (int i = 0; i < count; i++) {
        if (names[i] != NULL && strcmp(names[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void put_value(unsigned char *out, uint64_t value, int64_t width, int big_endian) {
    for (int64_t k = 0; k < width; k++) {
        unsigned char byte = k < 8 ? (unsigned char)(value >> (k * 8)) : 0;
        out[big_endian ? width - 1 - k : k] = byte;
    }
}

static int pack_part_slot(const gs_part *part, int64_t offset, int64_t size, unsigned char *out,
                          size_t *len, err *e) {
    int64_t alignment = part->alignment;
    int64_t shift = part->offset_shift;
    int64_t width = part->entry_size;
    int64_t field_size = part->field_size;
    const char *name = gs_path_name(part->container);

    if (part->field_count == 0 || field_size <= 0 || field_size * part->field_count != width) {
        err_set(e, "%s has no usable field description.", name);
        return 0;
    }
    if (!has_name(part->fields, part->field_count, "Offset")) {
        err_set(e, "%s stores no offsets, so an entry can't be pointed anywhere.", name);
        return 0;
    }
    if (alignment <= 0 || offset % alignment != 0) {
        err_set(e, "A payload landed at %lld, which isnt a %lld byte boundary.", (long long)offset,
                (long long)alignment);
        return 0;
    }
    if (width > RETURNS_SLOT_MAX) {
        err_set(e, "%s has a table entry wider than %d bytes.", name, RETURNS_SLOT_MAX);
        return 0;
    }
    uint64_t ceiling = field_size >= 8 ? UINT64_MAX : (((uint64_t)1 << (field_size * 8)) - 1);
    for (int i = 0; i < part->field_count; i++) {
        const char *field = part->fields[i];
        uint64_t v = 0;
        if (strcmp(field, "Offset") == 0) {
            v = (uint64_t)offset;
        } else if (strcmp(field, "Sector_Span") == 0) {
            v = (uint64_t)((size + alignment - 1) / alignment);
        } else if (strcmp(field, "Original_Size") == 0) {
            v = (uint64_t)size;
        }
        if (has_name(part->shift_fields, part->shift_field_count, field)) {
            v = shift >= 64 ? 0 : v >> shift;
        }
        if (v > ceiling) {
            err_set(e, "%s cant hold %s of %llu.", name, field, (unsigned long long)v);
            return 0;
        }
        put_value(out + i * field_size, v, field_size, part->big_endian);
    }
    *len = (size_t)width;
    return 1;
}

int returns_pack_slot(const gs_game *g, int64_t idx_marker, int64_t offset, int64_t size,
                      int64_t comp_marker, unsigned char *out, size_t *len, err *e) {
    const gs_part *part = gamedesc_part(g, idx_marker);
    if (part != NULL) {
        return pack_part_slot(part, offset, size, out, len, e);
    }
    int field_size = g->field_size;
    int entry_size = g->entry_size;
    int fields = field_size > 0 ? entry_size / field_size : 0;
    int step = field_size == 8 ? 1 : 2;
    if (field_size <= 0 || field_size * fields != entry_size || step * 3 >= fields) {
        err_set(e, "%s has a TOC layout this writer cant pack.", g->game_id);
        return 0;
    }
    if (field_size != 1 && field_size != 2 && field_size != 4 && field_size != 8) {
        err_set(e, "%s has a %d byte field this writer cant pack.", g->game_id, field_size);
        return 0;
    }
    if (entry_size > RETURNS_SLOT_MAX) {
        err_set(e, "%s has a table entry wider than %d bytes.", g->game_id, RETURNS_SLOT_MAX);
        return 0;
    }
    int64_t values[4] = {offset, size, size, comp_marker};
    uint64_t ceiling = field_size == 8 ? UINT64_MAX : (((uint64_t)1 << (field_size * 8)) - 1);
    memset(out, 0, (size_t)entry_size);
    for (int i = 0; i < 4; i++) {
        if (values[i] < 0 || (uint64_t)values[i] > ceiling) {
            err_set(e, "%s cant hold %lld in a %d byte field.", g->game_id, (long long)values[i],
                    field_size);
            return 0;
        }
        put_value(out + (size_t)(i * step) * (size_t)field_size, (uint64_t)values[i], field_size, 0);
    }
    *len = (size_t)entry_size;
    return 1;
}

static int has_sector_header(const gs_part *part) {
    return part != NULL && part->sector_field > 0;
}

static int refresh_sector_header(const gs_part *part, const char *idx_path, int64_t container_size,
                                 int64_t alignment, unsigned char *original, size_t *original_len,
                                 err *e) {
    int64_t at = part->sector_field;
    int64_t width = part->field_size != 0 ? part->field_size : 4;
    *original_len = 0;
    FILE *handle = file_open(idx_path, "r+b");
    if (handle == NULL) {
        char why[300];
        os_reason(why, sizeof(why));
        err_set(e, "couldnt update %s: %s", gs_path_name(idx_path), why);
        return -1;
    }
    size_t got = fread(original, 1, RETURNS_HEADER_WIDTH, handle);
    if ((int64_t)got < at + width) {
        fclose(handle);
        return 0;
    }
    uint64_t count = (uint64_t)(container_size / alignment);
    if (width < 8 && count >> (width * 8) != 0) {
        fclose(handle);
        err_set(e, "int too big to convert");
        return -1;
    }
    unsigned char raw[RETURNS_SLOT_MAX];
    if (width > RETURNS_SLOT_MAX) {
        fclose(handle);
        err_set(e, "couldnt update %s: field too wide", gs_path_name(idx_path));
        return -1;
    }
    put_value(raw, count, width, part->big_endian);
    int ok = _fseeki64(handle, at, SEEK_SET) == 0 && fwrite(raw, 1, (size_t)width, handle) == (size_t)width;
    if (fclose(handle) != 0) {
        ok = 0;
    }
    if (!ok) {
        err_set(e, "couldnt update %s: write failed", gs_path_name(idx_path));
        return -1;
    }
    *original_len = got;
    return 1;
}

int returns_capture_sizes(const gs_game *g, const char *game_dir, gs_tab *tab, int *measured,
                          err *e) {
    *measured = 0;
    if (tab_measured(tab)) {
        return 1;
    }
    for (int idx = 0; idx < g->container_count; idx++) {
        char *path = returns_container_file(g, game_dir, idx, g->containers[idx]);
        if (path != NULL && path_is_file(path)) {
            tab_remember_size(tab, idx, path_file_size(path));
        }
        free(path);
    }
    if (tab->size_count > 0) {
        if (!tab_save(tab, e)) {
            return 0;
        }
        *measured = 1;
    }
    return 1;
}

static int check_linkdata_target(const gs_recipe *r, const recipe_entry *entry, const gs_part *part,
                                 const char *bin_path, err *e) {
    const char *bin_name = gs_path_name(bin_path);
    if (entry->container != NULL && entry->container[0] != 0) {
        const char *built = gs_path_name(entry->container);
        if (!gs_casefold_equal(built, bin_name)) {
            err_set(e,
                    "%s was built against %s but this install has %s for that slot.\n\n"
                    "These are different regional versions of the game. Their tables may not "
                    "line up, so pouring it would corrupt the container. Use a mod built for "
                    "your region.",
                    r->name, built, bin_name);
            return 0;
        }
    }
    uint32_t magic = 0;
    uint32_t count = 0;
    if (!returns_read_linkdata_header(bin_path, &magic, &count) || !is_v2_magic(magic)) {
        err_set(e, "%s isnt a LINKDATA container.", bin_name);
        return 0;
    }
    int64_t table_end = part->toc_offset + (int64_t)count * part->entry_size;
    if (entry->entry_off >= table_end) {
        err_set(e,
                "%s points past the end of the %s table. It was built for a different version "
                "of the game.",
                r->name, bin_name);
        return 0;
    }
    return 1;
}

static int write_zeros(FILE *handle, int64_t count) {
    static const unsigned char zeros[4096];
    while (count > 0) {
        size_t take = count > (int64_t)sizeof(zeros) ? sizeof(zeros) : (size_t)count;
        if (fwrite(zeros, 1, take, handle) != take) {
            return 0;
        }
        count -= (int64_t)take;
    }
    return 1;
}

static int64_t py_mod(int64_t value, int64_t by) {
    int64_t m = value % by;
    if (m != 0 && ((m < 0) != (by < 0))) {
        m += by;
    }
    return m;
}

static int append_payload(const char *bin_path, const buf *payload, int64_t alignment,
                          int64_t *offset_out, int64_t *grown_out) {
    FILE *handle = file_open(bin_path, "r+b");
    if (handle == NULL) {
        return 0;
    }
    int ok = _fseeki64(handle, 0, SEEK_END) == 0;
    int64_t end = ok ? _ftelli64(handle) : -1;
    ok = ok && end >= 0;
    int64_t pad = ok ? py_mod(-end, alignment) : 0;
    ok = ok && write_zeros(handle, pad);
    int64_t offset = end + pad;
    ok = ok && (payload->len == 0 || fwrite(payload->data, 1, payload->len, handle) == payload->len);
    int64_t after = offset + (int64_t)payload->len;
    int64_t tail = ok ? py_mod(-after, alignment) : 0;
    ok = ok && write_zeros(handle, tail);
    if (fclose(handle) != 0) {
        ok = 0;
    }
    *offset_out = offset;
    *grown_out = after + tail;
    return ok;
}

static int write_at(const char *path, int64_t at, const void *data, size_t len) {
    FILE *handle = file_open(path, "r+b");
    if (handle == NULL) {
        return 0;
    }
    int ok = _fseeki64(handle, at, SEEK_SET) == 0 && (len == 0 || fwrite(data, 1, len, handle) == len);
    if (fclose(handle) != 0) {
        ok = 0;
    }
    return ok;
}

static int read_at(const char *path, int64_t at, unsigned char *out, size_t len, size_t *got) {
    *got = 0;
    FILE *handle = file_open(path, "rb");
    if (handle == NULL) {
        return 0;
    }
    if (_fseeki64(handle, at, SEEK_SET) == 0) {
        *got = fread(out, 1, len, handle);
    }
    fclose(handle);
    return 1;
}

int returns_apply(const gs_recipe *r, const gs_game *g, const char *game_dir, gs_tab *tab,
                  returns_progress progress, void *ctx, int64_t *poured, int *touched, err *e) {
    *poured = 0;
    *touched = 0;
    int measured = 0;
    if (!returns_capture_sizes(g, game_dir, tab, &measured, e)) {
        return 0;
    }
    size_t total = r->entry_count;
    json_value **slots = (json_value **)calloc(total + 1, sizeof(json_value *));
    if (slots == NULL) {
        err_set(e, "out of memory");
        return 0;
    }
    buf payload;
    buf_init(&payload);
    int ok = 1;
    size_t done = 0;
    char *bin_path = NULL;
    char *idx_path = NULL;

    for (size_t index = 0; index < total && ok; index++) {
        const recipe_entry *entry = &r->entries[index];
        free(bin_path);
        free(idx_path);
        bin_path = NULL;
        idx_path = NULL;
        if (!returns_check_pourable(g, entry->idx_marker, e)) {
            ok = 0;
            break;
        }
        const gs_part *part = gamedesc_part(g, entry->idx_marker);
        if (!returns_container_for(g, game_dir, entry->idx_marker, &bin_path, &idx_path, e)) {
            ok = 0;
            break;
        }
        if (!path_is_file(bin_path) || !path_is_file(idx_path)) {
            err_set(e, "Missing container %s or index %s.", gs_path_name(bin_path),
                    gs_path_name(idx_path));
            ok = 0;
            break;
        }
        if (returns_has_candidates(part) && !check_linkdata_target(r, entry, part, bin_path, e)) {
            ok = 0;
            break;
        }
        if (!recipe_read_payload(r, index, &payload, e)) {
            ok = 0;
            break;
        }
        int64_t width = part != NULL ? part->entry_size : g->entry_size;
        int64_t alignment = part != NULL ? part->alignment : RETURNS_ALIGNMENT;
        int64_t floor = part != NULL ? part->toc_offset : 0;
        if (width <= 0 || alignment <= 0) {
            err_set(e, "%s has a zero sized table entry or alignment.", g->game_id);
            ok = 0;
            break;
        }
        if (entry->entry_off < floor || py_mod(entry->entry_off - floor, width) != 0) {
            err_set(e, "%s points at %lld in %s, which isnt the start of a table entry.",
                    r->mod_id, (long long)entry->entry_off, gs_path_name(idx_path));
            ok = 0;
            break;
        }
        if (width > RETURNS_SLOT_MAX) {
            err_set(e, "%s has a table entry wider than %d bytes.", g->game_id, RETURNS_SLOT_MAX);
            ok = 0;
            break;
        }

        unsigned char original[RETURNS_SLOT_MAX];
        size_t original_len = 0;
        if (!read_at(idx_path, entry->entry_off, original, (size_t)width, &original_len)) {
            char why[300];
            os_reason(why, sizeof(why));
            err_set(e, "couldnt write %s: %s", gs_path_name(bin_path), why);
            ok = 0;
            break;
        }
        if ((int64_t)original_len < width) {
            err_set(e, "%s is too damn small for slot %lld.", gs_path_name(idx_path),
                    (long long)entry->entry_off);
            ok = 0;
            break;
        }

        int64_t offset = 0;
        int64_t grown = 0;
        if (!append_payload(bin_path, &payload, alignment, &offset, &grown)) {
            char why[300];
            os_reason(why, sizeof(why));
            err_set(e, "couldnt write %s: %s", gs_path_name(bin_path), why);
            *touched = 1;
            ok = 0;
            break;
        }
        *touched = 1;

        if (has_sector_header(part)) {
            unsigned char was[RETURNS_HEADER_WIDTH];
            size_t was_len = 0;
            int state = refresh_sector_header(part, idx_path, grown, alignment, was, &was_len, e);
            if (state < 0) {
                ok = 0;
                break;
            }
            if (state > 0) {
                char hex[RETURNS_HEADER_WIDTH * 2 + 1];
                hex_encode(was, was_len, hex);
                tab_remember_slot(tab, entry->idx_marker, RETURNS_HEADER_SLOT_OFF, hex);
            }
        }

        unsigned char packed[RETURNS_SLOT_MAX];
        size_t packed_len = 0;
        if (!returns_pack_slot(g, entry->idx_marker, offset, (int64_t)payload.len,
                               entry->comp_marker, packed, &packed_len, e)) {
            ok = 0;
            break;
        }
        if (!write_at(idx_path, entry->entry_off, packed, packed_len)) {
            char why[300];
            os_reason(why, sizeof(why));
            err_set(e, "couldnt write %s: %s", gs_path_name(bin_path), why);
            ok = 0;
            break;
        }

        char hex[RETURNS_SLOT_MAX * 2 + 1];
        hex_encode(original, (size_t)width, hex);
        tab_remember_slot(tab, entry->idx_marker, entry->entry_off, hex);

        char *keys[2] = {"idx_marker", "entry_off"};
        json_value *values[2] = {jv_num(&tab->a, (double)entry->idx_marker),
                                 jv_num(&tab->a, (double)entry->entry_off)};
        slots[done] = values[0] != NULL && values[1] != NULL ? jv_obj_from(&tab->a, keys, values, 2) : NULL;
        if (slots[done] == NULL) {
            err_set(e, "out of memory");
            ok = 0;
            break;
        }
        done++;
        if (progress != NULL) {
            progress(ctx, (int64_t)(index + 1), (int64_t)total);
        }
    }
    free(bin_path);
    free(idx_path);
    buf_free(&payload);

    if (ok) {
        if (progress != NULL) {
            progress(ctx, (int64_t)total, (int64_t)total);
        }
        json_value *list = jv_arr_from(&tab->a, slots, done);
        ok = list != NULL && tab_record(tab, r->mod_id, list);
        if (!ok) {
            err_set(e, "out of memory");
        }
        ok = ok && tab_save(tab, e);
        *poured = (int64_t)done;
    }
    free(slots);
    return ok;
}

int returns_restore_slots(const tab_slot *slots, size_t count, const gs_game *g,
                          const char *game_dir, int64_t *restored, err *e) {
    *restored = 0;
    buf original;
    buf_init(&original);
    int ok = 1;
    for (size_t i = 0; i < count; i++) {
        char *bin_path = NULL;
        char *idx_path = NULL;
        if (!returns_container_for(g, game_dir, slots[i].idx_marker, &bin_path, &idx_path, e)) {
            ok = 0;
            break;
        }
        if (path_is_file(idx_path)) {
            char why[300];
            if (slots[i].original == NULL) {
                gs_log("WARNING", "couldnt restore slot %lld: fromhex() argument must be str, not a value",
                       (long long)slots[i].entry_off);
            } else if (!hex_decode(slots[i].original, &original, why, sizeof(why))) {
                gs_log("WARNING", "couldnt restore slot %lld: %s", (long long)slots[i].entry_off, why);
            } else if (!write_at(idx_path, slots[i].entry_off, original.data, original.len)) {
                os_reason(why, sizeof(why));
                gs_log("WARNING", "couldnt restore slot %lld: %s", (long long)slots[i].entry_off, why);
            } else {
                (*restored)++;
            }
        }
        free(bin_path);
        free(idx_path);
    }
    buf_free(&original);
    return ok;
}

int returns_disable(const char *mod_id, const gs_game *g, const char *game_dir, gs_tab *tab,
                    int64_t *restored, err *e) {
    *restored = 0;
    json_value *slots = tab_forget(tab, mod_id);
    if (slots == NULL || slots->kind == JSON_NULL || slots->count == 0 ||
        (slots->kind != JSON_ARR && slots->kind != JSON_OBJ)) {
        return 1;
    }
    tab_slot *restorable = (tab_slot *)calloc(slots->count + 1, sizeof(tab_slot));
    if (restorable == NULL) {
        err_set(e, "out of memory");
        return 0;
    }
    size_t n = 0;
    for (size_t i = 0; slots->kind == JSON_ARR && i < slots->count; i++) {
        const json_value *slot = slots->items[i];
        const json_value *marker = json_obj_get(slot, "idx_marker");
        const json_value *offset = json_obj_get(slot, "entry_off");
        if (marker == NULL || offset == NULL) {
            continue;
        }
        int64_t idx_marker = json_as_i64(marker, 0);
        int64_t entry_off = json_as_i64(offset, 0);
        const json_value *original = tab_vanilla_for(tab, idx_marker, entry_off);
        if (original == NULL) {
            continue;
        }
        restorable[n].idx_marker = idx_marker;
        restorable[n].entry_off = entry_off;
        restorable[n].original = original->kind == JSON_STR ? original->text : NULL;
        n++;
    }
    int ok = returns_restore_slots(restorable, n, g, game_dir, restored, e);
    free(restorable);
    return ok && tab_save(tab, e);
}

int returns_disable_all(const gs_game *g, const char *game_dir, gs_tab *tab, int64_t *restored,
                        int64_t *trimmed, err *e) {
    *restored = 0;
    *trimmed = 0;
    size_t count = 0;
    tab_slot *slots = tab_all_vanilla_slots(tab, &count);
    if (slots == NULL) {
        err_set(e, "out of memory");
        return 0;
    }
    int ok = returns_restore_slots(slots, count, g, game_dir, restored, e);
    free(slots);
    if (!ok) {
        return 0;
    }
    for (size_t i = 0; i < tab->size_count; i++) {
        char *end = NULL;
        long long marker = strtoll(tab->sizes[i].key, &end, 10);
        if (end == tab->sizes[i].key) {
            err_set(e, "invalid literal for int() with base 10: '%s'", tab->sizes[i].key);
            return 0;
        }
        char *bin_path = NULL;
        char *idx_path = NULL;
        err skip;
        err_clear(&skip);
        if (!returns_container_for(g, game_dir, marker, &bin_path, &idx_path, &skip)) {
            continue;
        }
        if (path_is_file(bin_path)) {
            int64_t size = path_file_size(bin_path);
            if (size > tab->sizes[i].size) {
                if (file_truncate(bin_path, tab->sizes[i].size)) {
                    (*trimmed)++;
                } else {
                    char why[300];
                    os_reason(why, sizeof(why));
                    gs_log("WARNING", "couldnt trim %s: %s", gs_path_name(bin_path), why);
                }
            }
        }
        free(bin_path);
        free(idx_path);
    }
    tab_clear(tab);
    tab_clear_vanilla(tab);
    return tab_save(tab, e);
}

char **returns_containers_in(const gs_game *g, const char *folder, size_t *count) {
    *count = 0;
    char **found = (char **)calloc((size_t)(g->container_count + g->part_count + 1), sizeof(char *));
    if (found == NULL) {
        return NULL;
    }
    int any_candidates = 0;
    for (int i = 0; i < g->part_count; i++) {
        if (g->has_parts && g->parts[i].candidate_count > 0) {
            any_candidates = 1;
        }
    }
    if (any_candidates) {
        for (int i = 0; i < g->part_count; i++) {
            for (int k = 0; k < g->parts[i].candidate_count; k++) {
                char *path = path_join(folder, g->parts[i].candidates[k]);
                int hit = path != NULL && path_is_file(path);
                free(path);
                if (hit) {
                    found[(*count)++] = _strdup(g->parts[i].candidates[k]);
                    break;
                }
            }
        }
        return found;
    }
    for (int i = 0; i < g->container_count; i++) {
        char *path = path_join(folder, g->containers[i]);
        int hit = path != NULL && path_is_file(path);
        free(path);
        if (hit) {
            found[(*count)++] = _strdup(g->containers[i]);
        }
    }
    if (*count > 0 || g->container_count == 0) {
        return found;
    }
    if (!gs_wants_region_pair(g->containers[0])) {
        return found;
    }
    char *bin_name = NULL;
    char *toc_name = NULL;
    if (gs_region_pair(folder, &bin_name, &toc_name)) {
        found[(*count)++] = bin_name;
        free(toc_name);
    }
    return found;
}

void returns_names_free(char **names, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(names[i]);
    }
    free(names);
}
