#define WIN32_LEAN_AND_MEAN
#include "legacy.h"
#include "lzp2.h"
#include "names.h"
#include "subfolder.h"
#include "codec.h"
#include "repack.h"
#include "woz.h"
#include "wozindex.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LEGACY_MAX_THREADS 16
#define LEGACY_PD2_MAX_CHILDREN 4096


static const legacy_part dw4h_parts[] = {
    { LEGACY_TABLE_MDATA, "media\\linkdata.BIN", "media\\data\\etc\\mdata.bin",
      0x10, 0, 4, 16,
      4, 0, {"Offset", "Sector_Span", "Original_Size", "Unused_00"}, 4,
      11, {"Offset"}, 1,
      "Linkdata", 1, 1,
      LEGACY_SCAN_NONE, NULL, 0, 0, 0, 0, 0, 0 },
    { LEGACY_TABLE_SELF, "media\\data\\etc\\resource.bin", NULL,
      0x10, 4, 0, 8,
      4, 0, {"Offset", "Original_Size"}, 2,
      11, {"Offset"}, 1,
      "Resource", 0, 0,
      LEGACY_SCAN_NONE, NULL, 0, 0, LEGACY_SECTOR, 0, 0, 0 },
    { LEGACY_TABLE_NONE, "media\\data\\sound\\voice\\voice_jp.bns", NULL,
      0, 0, 0, 0,
      0, 0, {NULL}, 0, 0, {NULL}, 0,
      "Voice_JP", 0, 0,
      LEGACY_SCAN_NEXT_MAGIC, "OggS\x00\x02\x00\x00", 8, 0, LEGACY_SECTOR, 0, 0, 0 },
    { LEGACY_TABLE_NONE, "media\\data\\sound\\voice\\voice_us.bns", NULL,
      0, 0, 0, 0,
      0, 0, {NULL}, 0, 0, {NULL}, 0,
      "Voice_US", 0, 0,
      LEGACY_SCAN_NEXT_MAGIC, "OggS\x00\x02\x00\x00", 8, 0, LEGACY_SECTOR, 0, 0, 0 },
};

static const legacy_part sw2_parts[] = {
    { LEGACY_TABLE_MDATA, "linkdata\\LINKDATA_BNS_NA.lnk",
      "linkdata\\LINKDATA_BNS_NA.idx", 0x10, 4, 8, 16,
      4, 0, {"Offset", "Sector_Span", "Original_Size", "Unused_00"}, 4,
      11, {"Offset"}, 1,
      "BNS", 1, 1,
      LEGACY_SCAN_NONE, NULL, 0, 0, 0, 0, 0, 0 },
    { LEGACY_TABLE_MDATA, "linkdata\\LINKDATA_DNS_NA.lnk",
      "linkdata\\LINKDATA_DNS_NA.idx", 0x10, 4, 8, 16,
      4, 0, {"Offset", "Sector_Span", "Original_Size", "Unused_00"}, 4,
      11, {"Offset"}, 1,
      "DNS", 1, 1,
      LEGACY_SCAN_NONE, NULL, 0, 0, 0, 0, 0, 0 },
    { LEGACY_TABLE_NONE, "linkdata\\LINKDATA_ANS_NA.lnk", NULL,
      0, 0, 0, 0,
      0, 0, {NULL}, 0, 0, {NULL}, 0,
      "ANS", 0, 0,
      LEGACY_SCAN_SIZED, "KOVS", 4, 0, LEGACY_SECTOR, 32, 4, 0 },
    { LEGACY_TABLE_NONE, "linkdata\\LINK_BGM.lnk", NULL,
      0, 0, 0, 0,
      0, 0, {NULL}, 0, 0, {NULL}, 0,
      "BGM", 0, 0,
      LEGACY_SCAN_SIZED, "KOVS", 4, 164, 1, 32, 4, 0 },
    { LEGACY_TABLE_BANKS, "linkdata\\LINK_SEBANK_NA.lnk",
      "linkdata\\LINK_SEBANK_NA.idx", 0, 0, 0, 0,
      0, 0, {NULL}, 0, 0, {NULL}, 0,
      "SEBANK", 0, 0,
      LEGACY_SCAN_NONE, NULL, 0, 0, LEGACY_SECTOR, 0, 0, 0 },
};

typedef struct {
    const char *game_id;
    const legacy_part *parts;
    int count;
} legacy_game;

static const legacy_part dw6_parts[] = {
    { LEGACY_TABLE_ACCUM, "LINKDATA_UK.BIN", "LINKDATA_UK.IDX",
      0, 0, 0, 12,
      4, 0, {"Fallback_Size", "Original_Size", "Chain_Step"}, 3,
      0, {NULL}, 0,
      "Linkdata", 1, 0,
      LEGACY_SCAN_NONE, NULL, 0, 0, LEGACY_SECTOR, 0, 0, 0 },
};

static const legacy_part dw6e_parts[] = {
    { LEGACY_TABLE_ACCUM, "LINKDATA.BIN", "LINKDATA.IDX",
      0, 0, 0, 12,
      4, 1, {"Fallback_Size", "Original_Size", "Chain_Step"}, 3,
      0, {NULL}, 0,
      "Linkdata", 1, 0,
      LEGACY_SCAN_NONE, NULL, 0, 0, LEGACY_SECTOR, 0, 0, 0 },
};

static const legacy_part woz_parts[] = {
    { LEGACY_TABLE_ACCUM, "LINKDATA1.BIN", "LINKDATA1.IDX",
      0, 0, 0, 12,
      4, 0, {"Fallback_Size", "Original_Size", "Chunk_Offset"}, 3,
      0, {NULL}, 0,
      "Linkdata1", 1, 0,
      LEGACY_SCAN_NONE, NULL, 0, 0, LEGACY_SECTOR, 0, 0, 1,
      0x109B, 1 },
    { LEGACY_TABLE_ACCUM, "LINKDATA2.BIN", "LINKDATA2.IDX",
      0, 0, 0, 12,
      4, 0, {"Fallback_Size", "Original_Size", "Chunk_Offset"}, 3,
      0, {NULL}, 0,
      "Linkdata2", 1, 0,
      LEGACY_SCAN_NONE, NULL, 0, 0, LEGACY_SECTOR, 0, 0, 1,
      0x1327 - 0x109B, 1 },
    { LEGACY_TABLE_ACCUM, "LINKDATA3.BIN", "LINKDATA3.IDX",
      0, 0, 0, 12,
      4, 0, {"Fallback_Size", "Original_Size", "Chunk_Offset"}, 3,
      0, {NULL}, 0,
      "Linkdata3", 1, 0,
      LEGACY_SCAN_NONE, NULL, 0, 0, LEGACY_SECTOR, 0, 0, 1,
      0x161C - 0x1327, 1 },
    { LEGACY_TABLE_NONE, "data\\Sound\\Voice\\Voice.bin", NULL,
      0, 0, 0, 0,
      0, 0, {NULL}, 0, 0, {NULL}, 0,
      "Voice", 0, 0,
      LEGACY_SCAN_SIZED, "KOVS", 4, 0, 16, 32, 4, 0,
      0, 0 },
};

static const legacy_part wo1_parts[] = {
    { LEGACY_TABLE_MDATA, "data\\LINKDATA_BNS.LNK", "data\\LINKDATA_BNS.IDX",
      0x10, 4, 8, 16,
      4, 1, {"Offset", "Sector_Span", "Original_Size", "Unused_00"}, 4,
      11, {"Offset"}, 1,
      "BNS", 1, 1,
      LEGACY_SCAN_NONE, NULL, 0, 0, 0, 0, 0, 0 },
    { LEGACY_TABLE_MDATA, "data\\LINKDATA_ENS.LNK", "data\\LINKDATA_ENS.IDX",
      0x10, 4, 8, 16,
      4, 1, {"Offset", "Sector_Span", "Original_Size", "Unused_00"}, 4,
      11, {"Offset"}, 1,
      "ENS", 1, 1,
      LEGACY_SCAN_NONE, NULL, 0, 0, 0, 0, 0, 0 },
    { LEGACY_TABLE_MDATA, "data\\LINKDATA_FNS.LNK", "data\\LINKDATA_FNS.IDX",
      0x10, 4, 8, 16,
      4, 1, {"Offset", "Sector_Span", "Original_Size", "Unused_00"}, 4,
      11, {"Offset"}, 1,
      "FNS", 1, 1,
      LEGACY_SCAN_NONE, NULL, 0, 0, 0, 0, 0, 0 },
    { LEGACY_TABLE_MDATA, "data\\LINKDATA_GNS.LNK", "data\\LINKDATA_GNS.IDX",
      0x10, 4, 8, 16,
      4, 1, {"Offset", "Sector_Span", "Original_Size", "Unused_00"}, 4,
      11, {"Offset"}, 1,
      "GNS", 1, 1,
      LEGACY_SCAN_NONE, NULL, 0, 0, 0, 0, 0, 0 },
    { LEGACY_TABLE_MDATA, "data\\LINK_VODAT.BDX", "data\\LINKDATA_ANS.IDX",
      0x10, 4, 8, 16,
      4, 0, {"Offset", "Sector_Span", "Original_Size", "Unused_00"}, 4,
      11, {"Offset"}, 1,
      "ANS", 0, 1,
      LEGACY_SCAN_NONE, NULL, 0, 0, 0, 0, 0, 0 },
    { LEGACY_TABLE_NONE, "data\\LINK_BGM.BDX", NULL,
      0, 0, 0, 0,
      0, 0, {NULL}, 0, 0, {NULL}, 0,
      "BGM", 0, 0,
      LEGACY_SCAN_SIZED, "KOVS", 4, 0, LEGACY_SECTOR, 32, 4, 0 },
    { LEGACY_TABLE_BANKS, "data\\LINK_SEBANK.BDX", "data\\LINK_SEBANK.HDX",
      0, 0, 0, 0,
      0, 0, {NULL}, 0, 0, {NULL}, 0,
      "SEBANK", 0, 0,
      LEGACY_SCAN_NONE, NULL, 0, 0, LEGACY_SECTOR, 0, 0, 0 },
};

static const legacy_game legacy_games[] = {
    { "DW4H", dw4h_parts, (int)(sizeof(dw4h_parts) / sizeof(dw4h_parts[0])) },
    { "SW2", sw2_parts, (int)(sizeof(sw2_parts) / sizeof(sw2_parts[0])) },
    { "DW6", dw6_parts, (int)(sizeof(dw6_parts) / sizeof(dw6_parts[0])) },
    { "DW6E", dw6e_parts, (int)(sizeof(dw6e_parts) / sizeof(dw6e_parts[0])) },
    { "WO1", wo1_parts, (int)(sizeof(wo1_parts) / sizeof(wo1_parts[0])) },
    { "WOZ", woz_parts, (int)(sizeof(woz_parts) / sizeof(woz_parts[0])) },
};

const legacy_part *legacy_parts_for(const char *game_id, int *count) {
    for (size_t i = 0; i < sizeof(legacy_games) / sizeof(legacy_games[0]); i++) {
        if (game_id != NULL && strcmp(legacy_games[i].game_id, game_id) == 0) {
            if (count != NULL) {
                *count = legacy_games[i].count;
            }
            return legacy_games[i].parts;
        }
    }
    if (count != NULL) {
        *count = 0;
    }
    return NULL;
}

static int names_region_pair(const char *container, const char *toc) {
    if (container == NULL || toc == NULL) {
        return 0;
    }
    const char *slash = strrchr(container, '\\');
    const char *base = slash == NULL ? container : slash + 1;
    size_t len = strlen(base);
    if (len <= 13 || _strnicmp(base, "LINKDATA_", 9) != 0) {
        return 0;
    }
    if (_stricmp(base + len - 4, ".BIN") != 0) {
        return 0;
    }
    size_t tlen = strlen(toc);
    return tlen > 4 && _stricmp(toc + tlen - 4, ".IDX") == 0;
}

static int find_region_pair(const char *base_dir, char *bin_out, char *toc_out, size_t room) {
    char pattern[MAX_PATH];
    if (snprintf(pattern, sizeof(pattern), "%s\\LINKDATA_*.BIN", base_dir) >= (int)sizeof(pattern)) {
        return 0;
    }
    wchar_t *wide = path_to_wide(pattern);
    if (wide == NULL) {
        return 0;
    }
    WIN32_FIND_DATAW found;
    HANDLE handle = FindFirstFileW(wide, &found);
    free(wide);
    if (handle == INVALID_HANDLE_VALUE) {
        return 0;
    }
    int hit = 0;
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        char *name = wide_to_utf8(found.cFileName);
        if (name == NULL) {
            continue;
        }
        size_t len = strlen(name);
        if (len > 4 && len < room) {
            char toc[MAX_PATH];
            if (snprintf(toc, sizeof(toc), "%.*s.IDX", (int)(len - 4), name) < (int)sizeof(toc)) {
                char *probe = path_join(base_dir, toc);
                if (probe != NULL && path_is_file(probe)) {
                    snprintf(bin_out, room, "%s", name);
                    snprintf(toc_out, room, "%s", toc);
                    hit = 1;
                }
                free(probe);
            }
        }
        free(name);
    } while (!hit && FindNextFileW(handle, &found));
    FindClose(handle);
    return hit;
}

int legacy_region_pair(const char *base_dir, const char *container, const char *toc,
                       char *bin_out, char *toc_out, size_t room) {
    if (base_dir == NULL || !names_region_pair(container, toc)) {
        return 0;
    }
    char *declared = path_join(base_dir, container);
    int present = declared != NULL && path_is_file(declared);
    free(declared);
    if (present) {
        return 0;
    }
    return find_region_pair(base_dir, bin_out, toc_out, room);
}

const legacy_part *legacy_parts_in(job_ctx *job, const char *base_dir, const char *game_id,
                                   int *count, legacy_part_set *set) {
    const legacy_part *parts = legacy_parts_for(game_id, count);
    if (parts == NULL || base_dir == NULL || set == NULL) {
        return parts;
    }
    if (*count <= 0 || *count > LEGACY_PART_MAX) {
        return parts;
    }
    memcpy(set->items, parts, sizeof(legacy_part) * (size_t)*count);
    set->count = *count;
    int swapped = 0;
    for (int i = 0; i < *count; i++) {
        if (!legacy_region_pair(base_dir, parts[i].container, parts[i].toc,
                                set->bins[i], set->tocs[i], sizeof(set->bins[i]))) {
            continue;
        }
        if (job != NULL) {
            emit_log(job, "info", "%s: %s isnt here, using %s and %s instead",
                     parts[i].pack, parts[i].container, set->bins[i], set->tocs[i]);
        }
        set->items[i].container = set->bins[i];
        set->items[i].toc = set->tocs[i];
        swapped = 1;
    }
    return swapped ? set->items : parts;
}

const legacy_part *legacy_part_at(const char *game_id, int index) {
    int count = 0;
    const legacy_part *list = legacy_parts_for(game_id, &count);
    if (list == NULL || index < 0 || index >= count) {
        return NULL;
    }
    return &list[index];
}

int legacy_part_count(const char *game_id) {
    int count = 0;
    legacy_parts_for(game_id, &count);
    return count;
}

static void put_u32(unsigned char *at, uint32_t v) {
    at[0] = (unsigned char)(v & 0xFF);
    at[1] = (unsigned char)((v >> 8) & 0xFF);
    at[2] = (unsigned char)((v >> 16) & 0xFF);
    at[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void legacy_write_named(const legacy_part *p, unsigned char *raw,
                               const char *name, uint64_t value);

int legacy_field_index(const legacy_part *p, const char *name) {
    for (int i = 0; i < p->field_count; i++) {
        if (strcmp(p->fields[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

uint64_t legacy_read_field(const legacy_part *p, const unsigned char *raw, int index) {
    if (index < 0 || p->field_size <= 0) {
        return 0;
    }
    const unsigned char *at = raw + (size_t)index * (size_t)p->field_size;
    uint64_t v = 0;
    if (p->big_endian) {
        for (int i = 0; i < p->field_size; i++) {
            v = (v << 8) | at[i];
        }
    } else {
        for (int i = p->field_size - 1; i >= 0; i--) {
            v = (v << 8) | at[i];
        }
    }
    return v;
}

static int legacy_field_shifted(const legacy_part *p, const char *name) {
    if (p->shift_bits == 0) {
        return 0;
    }
    for (int i = 0; i < p->shift_field_count; i++) {
        if (strcmp(p->shift_fields[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

uint64_t legacy_read_named(const legacy_part *p, const unsigned char *raw, const char *name) {
    int index = legacy_field_index(p, name);
    if (index < 0) {
        return 0;
    }
    uint64_t v = legacy_read_field(p, raw, index);
    return legacy_field_shifted(p, name) ? (v << p->shift_bits) : v;
}

static void legacy_write_named(const legacy_part *p, unsigned char *raw,
                               const char *name, uint64_t value) {
    int index = legacy_field_index(p, name);
    if (index < 0) {
        return;
    }
    if (legacy_field_shifted(p, name)) {
        value >>= p->shift_bits;
    }
    unsigned char *at = raw + (size_t)index * (size_t)p->field_size;
    if (p->big_endian) {
        for (int i = p->field_size - 1; i >= 0; i--) {
            at[i] = (unsigned char)(value & 0xFF);
            value >>= 8;
        }
    } else {
        for (int i = 0; i < p->field_size; i++) {
            at[i] = (unsigned char)(value & 0xFF);
            value >>= 8;
        }
    }
}

static uint32_t read_u32(const unsigned char *at) {
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8) |
           ((uint32_t)at[2] << 16) | ((uint32_t)at[3] << 24);
}


static int pd2_read(const unsigned char *blob, size_t len, arena *a,
                    size_t **offs_out, size_t **sizes_out, size_t *count_out,
                    size_t *end_out) {
    if (len < 8) {
        return 0;
    }
    uint32_t count = read_u32(blob);
    if (count == 0 || count > LEGACY_PD2_MAX_CHILDREN) {
        return 0;
    }
    size_t table_end = 4 + (size_t)count * 4;
    if (table_end > len) {
        return 0;
    }

    size_t *offs = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    size_t *sizes = (size_t *)arena_alloc(a, sizeof(size_t) * count);
    if (offs == NULL || sizes == NULL) {
        return 0;
    }

    size_t cursor = table_end;
    cursor += (16 - (cursor % 16)) % 16;

    for (uint32_t i = 0; i < count; i++) {
        uint64_t size = (uint64_t)read_u32(blob + 4 + (size_t)i * 4) << 4;
        if (size > len || cursor > len || (uint64_t)(len - cursor) < size) {
            return 0;
        }
        offs[i] = cursor;
        sizes[i] = (size_t)size;
        cursor += (size_t)size;
    }

    *offs_out = offs;
    *sizes_out = sizes;
    *count_out = count;
    if (end_out != NULL) {
        *end_out = cursor;
    }
    return 1;
}

int legacy_pd2_parse(const unsigned char *blob, size_t len, arena *a,
                   size_t **offs_out, size_t **sizes_out, size_t *count_out) {
    return pd2_read(blob, len, a, offs_out, sizes_out, count_out, NULL);
}

int legacy_looks_like_pd2(const unsigned char *blob, size_t len) {
    arena a;
    arena_init(&a);
    size_t *offs = NULL;
    size_t *sizes = NULL;
    size_t count = 0;
    size_t end = 0;
    int ok = pd2_read(blob, len, &a, &offs, &sizes, &count, &end) && end == len;
    arena_free(&a);
    return ok;
}

const char *legacy_pd2_child_ext(const unsigned char *data, size_t len) {
    if (len >= 3 && memcmp(data, "\xFF\xD8\xFF", 3) == 0) {
        return ".jpg";
    }
    if (len >= 10 && memcmp(data + 6, "JFIF", 4) == 0) {
        return ".jpg";
    }
    return codec_resolve_ext(data, len, NULL);
}


typedef struct {
    HANDLE file;
    HANDLE mapping;
    const unsigned char *base;
    int64_t size;
} mapped;

static void mapped_close(mapped *m) {
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

static int mapped_open(mapped *m, const char *path, err *e) {
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
    if (!GetFileSizeEx(m->file, &size) || size.QuadPart == 0) {
        mapped_close(m);
        err_set(e, "couldnt size %s", path);
        return 0;
    }
    m->size = (int64_t)size.QuadPart;
    m->mapping = CreateFileMappingW(m->file, NULL, PAGE_READONLY, 0, 0, NULL);
    if (m->mapping == NULL) {
        mapped_close(m);
        err_set(e, "couldnt map %s", path);
        return 0;
    }
    m->base = (const unsigned char *)MapViewOfFile(m->mapping, FILE_MAP_READ, 0, 0, 0);
    if (m->base == NULL) {
        mapped_close(m);
        err_set(e, "couldnt view %s", path);
        return 0;
    }
    return 1;
}

typedef struct {
    int64_t offset;
    int64_t size;
    int64_t file_index;
    int64_t slot_index;
    int64_t toc_off;
    char *rel;
    const char *ext;
    char ext_store[8];
    int rate;
    int channels;
    const char *name;
    int is_pd2;
    int own_folder;
    int64_t unpacked_size;
    int lzp2;
    float lzp2_version;
    int64_t lzp2_chunk;
} entry_plan;

typedef struct {
    job_ctx *job;
    const unpack_opts *opts;
    const legacy_part *part;
    const mapped *bin;
    const char *bin_name;
    const char *pack_dir;
    entry_plan *plans;
    int64_t count;

    volatile LONG cursor;
    volatile LONG failed;
    CRITICAL_SECTION error_lock;
    err first_error;

    volatile LONG64 files_written;
    volatile LONG64 nested_written;

    int64_t total_bytes;
    volatile LONG64 *done_bytes;
} part_work;

static const char *printable_magic_ext(const unsigned char *data, size_t len, char out[8]) {
    if (len < 4) {
        return NULL;
    }
    out[0] = '.';
    for (int i = 0; i < 4; i++) {
        unsigned char ch = data[i];
        int ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                 (ch >= '0' && ch <= '9');
        if (!ok) {
            return NULL;
        }
        out[1 + i] = (char)(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    }
    out[5] = 0;
    return out;
}

static int write_pd2_children(job_ctx *job, const char *file_path,
                              const unsigned char *data, size_t len, int64_t *written) {
    arena a;
    arena_init(&a);
    size_t *offs = NULL;
    size_t *sizes = NULL;
    size_t count = 0;
    if (!pd2_read(data, len, &a, &offs, &sizes, &count, NULL)) {
        arena_free(&a);
        return 0;
    }

    const char *slash = strrchr(file_path, '\\');
    const char *fname = slash == NULL ? file_path : slash + 1;
    const char *dot = strrchr(fname, '.');
    size_t dir_len = slash == NULL ? 0 : (size_t)(slash - file_path);
    size_t stem_len = dot == NULL ? strlen(fname) : (size_t)(dot - fname);

    char *out_dir = (char *)malloc(dir_len + 1 + stem_len + 1);
    if (out_dir == NULL) {
        arena_free(&a);
        return 0;
    }
    if (dir_len > 0) {
        memcpy(out_dir, file_path, dir_len);
        out_dir[dir_len] = '\\';
        memcpy(out_dir + dir_len + 1, fname, stem_len);
        out_dir[dir_len + 1 + stem_len] = 0;
    } else {
        memcpy(out_dir, fname, stem_len);
        out_dir[stem_len] = 0;
    }

    if (!path_make_dirs(out_dir)) {
        free(out_dir);
        arena_free(&a);
        return 0;
    }

    for (size_t i = 0; i < count; i++) {
        if (job != NULL && job_cancelled(job)) {
            break;
        }
        const unsigned char *child = data + offs[i];
        const char *ext = legacy_pd2_child_ext(child, sizes[i]);
        char name[64];
        snprintf(name, sizeof(name), "entry_%05llu%s", (unsigned long long)i, ext);
        char *path = path_join(out_dir, name);
        if (path == NULL) {
            break;
        }
        if (file_write_prepared(path, child, sizes[i])) {
            (*written)++;
        }
        free(path);
    }

    free(out_dir);
    arena_free(&a);
    return 1;
}

static DWORD WINAPI part_worker(LPVOID param) {
    part_work *w = (part_work *)param;
    int64_t files_local = 0;
    int64_t nested_local = 0;
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

        entry_plan *plan = &w->plans[taken];
        const unsigned char *data = w->bin->base + plan->offset;
        size_t len = (size_t)plan->size;

        if (w->opts->write_files && plan->lzp2) {
            err expand = {0};
            if (lzp2_decompress_chain(data, len, &plain, &expand)) {
                data = (const unsigned char *)plain.data;
                len = plain.len;
            } else {
                emit_log(w->job, "warn", "%s didnt decompress (%s); writing it as stored",
                         plan->rel, expand.set ? expand.text : "unknown");
            }
        }

        if (w->opts->write_files) {
            char *path = path_join(w->pack_dir, plan->rel);
            if (path == NULL) {
                EnterCriticalSection(&w->error_lock);
                if (!w->first_error.set) {
                    err_set(&w->first_error, "out of memory building an output path");
                }
                LeaveCriticalSection(&w->error_lock);
                InterlockedExchange(&w->failed, 1);
                break;
            }
            if (!file_write_prepared(path, data, len)) {
                EnterCriticalSection(&w->error_lock);
                if (!w->first_error.set) {
                    err_set(&w->first_error, "couldnt write %s", plan->rel);
                }
                LeaveCriticalSection(&w->error_lock);
                InterlockedExchange(&w->failed, 1);
                free(path);
                break;
            }
            files_local++;
            char *pseudo = plan->own_folder ? subfolder_pseudo_path(path) : NULL;
            const char *nest_at = pseudo != NULL ? pseudo : path;
            int split_as_pd2 = plan->is_pd2 && write_pd2_children(w->job, nest_at, data, len, &nested_local);
            if (!split_as_pd2 && w->part->deep_nested) {
                nested_unpack_resource(w->job, nest_at, data, len, 0, &nested_local);
            }
            free(pseudo);
            free(path);
        }

        LONG64 done = InterlockedAdd64(w->done_bytes, (LONG64)plan->size);
        if ((taken & 63) == 0) {
            emit_progress(w->job, done, w->total_bytes, w->bin_name);
        }
    }

    buf_free(&plain);
    InterlockedAdd64(&w->files_written, files_local);
    InterlockedAdd64(&w->nested_written, nested_local);
    return 0;
}

static int plan_table_part(const legacy_part *part, const unsigned char *toc,
                           size_t toc_len, int64_t bin_size, const name_list *names,
                           entry_plan **plans_out, int64_t *count_out,
                           int64_t *seen_out, int64_t *skipped_out,
                           job_ctx *job, err *e) {
    int64_t declared = 0;
    if (toc_len >= (size_t)part->count_offset + 4) {
        declared = (int64_t)read_u32(toc + part->count_offset);
    }
    int64_t capacity = 0;
    if (toc_len > (size_t)part->toc_offset) {
        capacity = (int64_t)((toc_len - (size_t)part->toc_offset) / (size_t)part->entry_size);
    }
    if (declared <= 0 || declared > capacity) {
        emit_log(job, "warn", "%s declares %lld entries but holds room for %lld; using the smaller",
                 part->container, (long long)declared, (long long)capacity);
        declared = capacity;
    }
    if (declared <= 0) {
        err_set(e, "%s has no usable table", part->container);
        return 0;
    }

    entry_plan *plans = (entry_plan *)calloc((size_t)declared, sizeof(entry_plan));
    if (plans == NULL) {
        err_set(e, "out of memory planning %lld entries", (long long)declared);
        return 0;
    }

    int64_t kept = 0;
    int64_t skipped = 0;
    for (int64_t i = 0; i < declared; i++) {
        const unsigned char *raw = toc + part->toc_offset + i * part->entry_size;
        int64_t offset = (int64_t)legacy_read_named(part, raw, "Offset");
        int64_t size = (int64_t)legacy_read_named(part, raw, "Original_Size");
        int64_t toc_off = part->toc_offset + i * part->entry_size;

        if (size <= 0) {
            skipped++;
            continue;
        }
        if (offset < 0 || offset > bin_size || bin_size - offset < size) {
            emit_log(job, "warn", "%s entry %lld runs past the end; skipping",
                     part->container, (long long)i);
            skipped++;
            continue;
        }

        entry_plan *plan = &plans[kept];
        plan->slot_index = i;
        plan->offset = offset;
        plan->size = size;
        plan->toc_off = toc_off;
        plan->file_index = kept;
        plan->name = part->named ? name_at(names, i) : NULL;
        kept++;
    }

    *plans_out = plans;
    *count_out = kept;
    *seen_out = declared;
    *skipped_out = skipped;
    return 1;
}


static int plan_bank_part(const legacy_part *part, const mapped *bin,
                          const unsigned char *toc, size_t toc_len,
                          entry_plan **plans_out, int64_t *count_out,
                          int64_t *seen_out, job_ctx *job, err *e) {
    int64_t cap = 256;
    entry_plan *plans = (entry_plan *)calloc((size_t)cap, sizeof(entry_plan));
    if (plans == NULL) {
        err_set(e, "out of memory planning %s", part->container);
        return 0;
    }

    int64_t kept = 0;
    int64_t bank_start = 0;
    size_t at = 0;
    int banks = 0;

    while (at + 12 <= toc_len) {
        uint32_t data_len = read_u32(toc + at + 4);
        uint32_t first = read_u32(toc + at + 8);
        if (read_u32(toc + at) == 0 && data_len == 0 && first == 0) {
            break;
        }
        if (first < 12 || (first - 8) % 4 != 0) {
            break;
        }
        int64_t n = (int64_t)((first - 8) / 4);
        if (n <= 0 || at + first > toc_len) {
            break;
        }

        int64_t furthest = 0;
        for (int64_t i = 0; i < n; i++) {
            size_t rec = at + read_u32(toc + at + 8 + (size_t)i * 4);
            if (rec + 40 > toc_len) {
                break;
            }
            if ((int64_t)rec > furthest) {
                furthest = (int64_t)rec;
            }
            uint32_t rate = read_u32(toc + rec + 4) & 0xFFFF;
            uint32_t align = read_u32(toc + rec + 8) & 0xFFFF;
            uint32_t samples = read_u32(toc + rec + 16);
            uint32_t where = read_u32(toc + rec + 20);
            uint32_t blen = read_u32(toc + rec + 24);

            if (align == 0 || blen != samples * align || where + blen > data_len) {
                emit_log(job, "warn", "%s bank %d entry %lld doesnt describe audio, skipping",
                         part->pack, banks, (long long)i);
                continue;
            }
            if (bank_start + where + blen > bin->size) {
                emit_log(job, "warn", "%s bank %d entry %lld runs past the container",
                         part->pack, banks, (long long)i);
                continue;
            }

            if (kept == cap) {
                cap *= 2;
                entry_plan *grown = (entry_plan *)realloc(plans, sizeof(entry_plan) * (size_t)cap);
                if (grown == NULL) {
                    free(plans);
                    err_set(e, "out of memory planning %s", part->container);
                    return 0;
                }
                memset(grown + kept, 0, sizeof(entry_plan) * (size_t)(cap - kept));
                plans = grown;
            }
            entry_plan *plan = &plans[kept];
            plan->offset = bank_start + where;
            plan->size = blen;
            plan->file_index = kept;
            plan->slot_index = kept;
            plan->toc_off = (int64_t)rec;
            plan->ext = ".pcm";
            plan->rate = (int)rate;
            plan->channels = align >= 4 ? 2 : 1;
            kept++;
        }

        banks++;
        bank_start = (bank_start + data_len + LEGACY_SECTOR - 1) /
                     LEGACY_SECTOR * LEGACY_SECTOR;
        at = (size_t)((furthest + 40 + LEGACY_SECTOR - 1) / LEGACY_SECTOR * LEGACY_SECTOR);
    }

    if (kept == 0) {
        free(plans);
        err_set(e, "no sounds found in %s", part->container);
        return 0;
    }
    emit_log(job, "info", "%s: %d banks holding %lld sounds",
             part->pack, banks, (long long)kept);

    *plans_out = plans;
    *count_out = kept;
    *seen_out = kept;
    return 1;
}

static int plan_accum_part(const legacy_part *part, const mapped *bin,
                           const unsigned char *toc, size_t toc_len,
                           const name_list *names, entry_plan **plans_out,
                           int64_t *count_out, int64_t *seen_out,
                           int64_t *skipped_out, job_ctx *job, err *e) {
    int64_t capacity = (int64_t)(toc_len / (size_t)part->entry_size);
    if (part->row_count > 0 && part->row_count < capacity) {
        capacity = part->row_count;
    }
    if (capacity <= 0) {
        err_set(e, "%s has no usable table", part->container);
        return 0;
    }

    entry_plan *plans = (entry_plan *)calloc((size_t)capacity, sizeof(entry_plan));
    if (plans == NULL) {
        err_set(e, "out of memory planning %lld entries", (long long)capacity);
        return 0;
    }

    int64_t kept = 0, seen = 0, skipped = 0, run = 0;
    for (int64_t i = 0; i < capacity && run < bin->size; i++) {
        const unsigned char *raw = toc + (size_t)i * (size_t)part->entry_size;
        uint64_t span = legacy_read_named(part, raw, "Original_Size");
        if (span == 0) {
            span = legacy_read_named(part, raw, "Fallback_Size");
        }
        seen++;
        if (span == 0) {
            skipped++;
            continue;
        }
        int64_t step = ((int64_t)span + LEGACY_SECTOR - 1) / LEGACY_SECTOR * LEGACY_SECTOR;
        if (run + (int64_t)span > bin->size) {
            emit_log(job, "warn", "%s entry %lld runs past the end, skipping",
                     part->container, (long long)i);
            skipped++;
            run += step;
            continue;
        }

        entry_plan *plan = &plans[kept];
        plan->offset = run;
        plan->size = (int64_t)span;
        plan->toc_off = (int64_t)i * part->entry_size;
        plan->file_index = kept;
        plan->slot_index = i;
        plan->name = part->named ? name_at(names, i) : NULL;
        kept++;
        run += step;
    }

    emit_log(job, "info", "%s: %lld entries covering %lld of %lld bytes",
             part->pack, (long long)kept, (long long)run, (long long)bin->size);
    *plans_out = plans;
    *count_out = kept;
    *seen_out = seen;
    *skipped_out = skipped;
    return 1;
}

static int plan_scan_part(const legacy_part *part, const mapped *bin,
                          entry_plan **plans_out, int64_t *count_out, err *e) {
    int64_t align = part->scan_align > 0 ? part->scan_align : 1;
    int64_t cap = 64;
    int64_t *starts = (int64_t *)malloc(sizeof(int64_t) * (size_t)cap);
    int64_t *sizes = (int64_t *)malloc(sizeof(int64_t) * (size_t)cap);
    if (starts == NULL || sizes == NULL) {
        free(starts);
        free(sizes);
        err_set(e, "out of memory scanning %s", part->container);
        return 0;
    }

    int64_t found = 0;
    int ok = 1;
    if (part->scan == LEGACY_SCAN_SIZED) {
        int64_t at = part->scan_start;
        while (at + part->sized_header <= bin->size) {
            if (memcmp(bin->base + at, part->scan_magic, (size_t)part->scan_magic_len) != 0) {
                break;
            }
            int64_t len = (int64_t)read_u32(bin->base + at + part->sized_len_off);
            int64_t whole = part->sized_header + len;
            if (len < 0 || whole <= 0 || at + whole > bin->size) {
                err_set(e, "%s declares an entry longer than the container at %lld",
                        part->container, (long long)at);
                ok = 0;
                break;
            }
            if (found == cap) {
                cap *= 2;
                int64_t *a = (int64_t *)realloc(starts, sizeof(int64_t) * (size_t)cap);
                int64_t *b = (int64_t *)realloc(sizes, sizeof(int64_t) * (size_t)cap);
                if (a == NULL || b == NULL) {
                    free(a != NULL ? a : starts);
                    free(b != NULL ? b : sizes);
                    err_set(e, "out of memory scanning %s", part->container);
                    return 0;
                }
                starts = a;
                sizes = b;
            }
            starts[found] = at;
            sizes[found] = whole;
            found++;
            at += whole;
            at += (align - (at % align)) % align;
        }
    } else {
        for (int64_t at = part->scan_start; at + part->scan_magic_len <= bin->size; at += align) {
            if (memcmp(bin->base + at, part->scan_magic, (size_t)part->scan_magic_len) != 0) {
                continue;
            }
            if (found == cap) {
                cap *= 2;
                int64_t *a = (int64_t *)realloc(starts, sizeof(int64_t) * (size_t)cap);
                int64_t *b = (int64_t *)realloc(sizes, sizeof(int64_t) * (size_t)cap);
                if (a == NULL || b == NULL) {
                    free(a != NULL ? a : starts);
                    free(b != NULL ? b : sizes);
                    err_set(e, "out of memory scanning %s", part->container);
                    return 0;
                }
                starts = a;
                sizes = b;
            }
            starts[found++] = at;
        }
        for (int64_t i = 0; i < found; i++) {
            sizes[i] = (i + 1 < found ? starts[i + 1] : bin->size) - starts[i];
        }
    }

    if (ok && found == 0) {
        err_set(e, "nothing found by scanning %s", part->container);
        ok = 0;
    }
    if (!ok) {
        free(starts);
        free(sizes);
        return 0;
    }
    if (found <= 0 || found > bin->size) {
        free(starts);
        free(sizes);
        err_set(e, "%s scanned to an impossible entry count", part->container);
        return 0;
    }

    entry_plan *plans = (entry_plan *)calloc((size_t)found, sizeof(entry_plan));
    if (plans == NULL) {
        free(starts);
        free(sizes);
        err_set(e, "out of memory planning %s", part->container);
        return 0;
    }
    for (int64_t i = 0; i < found; i++) {
        plans[i].offset = starts[i];
        plans[i].size = sizes[i];
        plans[i].file_index = i;
        plans[i].slot_index = i;
        plans[i].toc_off = -1;
    }
    free(starts);
    free(sizes);

    *plans_out = plans;
    *count_out = found;
    return 1;
}

static void free_plans(entry_plan *plans, int64_t count) {
    for (int64_t i = 0; i < count; i++) {
        free(plans[i].rel);
    }
    free(plans);
}

static int disambiguate_names(entry_plan *plans, int64_t count, err *e) {
    char **rel = (char **)malloc(sizeof(char *) * (size_t)(count > 0 ? count : 1));
    int64_t *slot = (int64_t *)malloc(sizeof(int64_t) * (size_t)(count > 0 ? count : 1));
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
    int *own = ok ? (int *)calloc((size_t)(count > 0 ? count : 1), sizeof(int)) : NULL;
    if (ok && (own == NULL || !subfolder_mark_shared(rel, count, own))) {
        err_set(e, "out of memory checking for shared subcontainer folders");
        ok = 0;
    }
    for (int64_t i = 0; ok && i < count; i++) {
        plans[i].own_folder = own[i];
    }
    free(own);
    free(rel);
    free(slot);
    return ok;
}

static int finish_plans(const legacy_part *part, const mapped *bin, entry_plan *plans,
                        int64_t count, const char *pack_dir, int write_files, err *e) {
    for (int64_t i = 0; i < count; i++) {
        entry_plan *plan = &plans[i];
        const unsigned char *data = bin->base + plan->offset;
        size_t len = (size_t)plan->size;

        plan->unpacked_size = plan->size;
        if (lzp2_looks_like(data, len)) {
            plan->lzp2 = 1;
            plan->lzp2_version = lzp2_version(data, len);
            plan->lzp2_chunk = (int64_t)lzp2_chain_chunk(data, len);
            plan->unpacked_size = (int64_t)lzp2_chain_original(data, len);

            buf head;
            buf_init(&head);
            err peek = {0};
            if (lzp2_decompress_prefix(data, len, &head, 64, &peek)) {
                plan->ext = codec_resolve_ext((const unsigned char *)head.data,
                                              head.len, NULL);
                if (plan->ext != NULL) {
                    snprintf(plan->ext_store, sizeof(plan->ext_store), "%s", plan->ext);
                    plan->ext = plan->ext_store;
                }
            }
            buf_free(&head);
        }

        if (plan->ext == NULL) {
            const char *from_magic = part->table == LEGACY_TABLE_SELF
                                         ? printable_magic_ext(data, len, plan->ext_store)
                                         : NULL;
            plan->ext = from_magic != NULL ? plan->ext_store
                                           : codec_resolve_ext(data, len, NULL);
        }

        if (plan->name != NULL) {
            char *safe = path_sanitize_relative(plan->name);
            if (safe == NULL) {
                err_set(e, "out of memory naming entry %lld", (long long)i);
                return 0;
            }
            plan->rel = safe;
            plan->is_pd2 = len > 8 && strlen(plan->name) > 4 &&
                           _stricmp(plan->name + strlen(plan->name) - 4, ".pd2") == 0;
        } else {
            char name[64];
            snprintf(name, sizeof(name), "entry_%05lld%s",
                     (long long)plan->file_index, plan->ext);
            plan->rel = _strdup(name);
            if (plan->rel == NULL) {
                err_set(e, "out of memory naming entry %lld", (long long)i);
                return 0;
            }
        }
    }

    if (!disambiguate_names(plans, count, e)) {
        return 0;
    }

    if (!write_files) {
        return 1;
    }

    if (!path_make_dirs(pack_dir)) {
        err_set(e, "couldnt create %s", pack_dir);
        return 0;
    }
    for (int64_t i = 0; i < count; i++) {
        if (strchr(plans[i].rel, '\\') == NULL) {
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

static int run_part(job_ctx *job, const unpack_opts *opts, int idx_marker,
                    const legacy_part *part, const name_list *names,
                    manifest_writer *manifest, unpack_stats *stats,
                    int64_t total_bytes, volatile LONG64 *done_bytes, err *e) {
    char *bin_path = path_join(opts->base_dir, part->container);
    if (bin_path == NULL) {
        err_set(e, "out of memory building a container path");
        return 0;
    }
    if (!path_is_file(bin_path)) {
        emit_log(job, "warn", "%s isnt in that folder, skipping it", part->container);
        free(bin_path);
        return 1;
    }

    mapped bin;
    if (!mapped_open(&bin, bin_path, e)) {
        free(bin_path);
        return 0;
    }

    const char *bin_name = strrchr(part->container, '\\');
    bin_name = bin_name == NULL ? part->container : bin_name + 1;

    entry_plan *plans = NULL;
    int64_t count = 0;
    int64_t seen = 0;
    int64_t skipped = 0;
    int ok = 1;

    if (part->table == LEGACY_TABLE_NONE) {
        ok = plan_scan_part(part, &bin, &plans, &count, e);
        seen = count;
    } else if (part->table == LEGACY_TABLE_BANKS) {
        char *toc_path = path_join(opts->base_dir, part->toc);
        if (toc_path == NULL) {
            mapped_close(&bin);
            free(bin_path);
            err_set(e, "out of memory building a table path");
            return 0;
        }
        buf toc;
        buf_init(&toc);
        if (!file_read_all(toc_path, &toc)) {
            err_set(e, "couldnt read %s", part->toc);
            ok = 0;
        } else {
            ok = plan_bank_part(part, &bin, (const unsigned char *)toc.data, toc.len,
                                &plans, &count, &seen, job, e);
        }
        buf_free(&toc);
        free(toc_path);
    } else if (part->table == LEGACY_TABLE_ACCUM) {
        char *toc_path = path_join(opts->base_dir, part->toc);
        if (toc_path == NULL) {
            mapped_close(&bin);
            free(bin_path);
            err_set(e, "out of memory building a table path");
            return 0;
        }
        buf toc;
        buf_init(&toc);
        if (!file_read_all(toc_path, &toc)) {
            err_set(e, "couldnt read %s", part->toc);
            ok = 0;
        } else {
            ok = plan_accum_part(part, &bin, (const unsigned char *)toc.data, toc.len,
                                 names, &plans, &count, &seen, &skipped, job, e);
        }
        buf_free(&toc);
        free(toc_path);
    } else if (part->table == LEGACY_TABLE_SELF) {
        ok = plan_table_part(part, bin.base, (size_t)bin.size, bin.size, names,
                             &plans, &count, &seen, &skipped, job, e);
    } else {
        char *toc_path = path_join(opts->base_dir, part->toc);
        if (toc_path == NULL) {
            mapped_close(&bin);
            free(bin_path);
            err_set(e, "out of memory building a table path");
            return 0;
        }
        buf toc;
        buf_init(&toc);
        if (!file_read_all(toc_path, &toc)) {
            err_set(e, "couldnt read %s", part->toc);
            ok = 0;
        } else {
            ok = plan_table_part(part, (const unsigned char *)toc.data, toc.len,
                                 bin.size, names, &plans, &count, &seen, &skipped, job, e);
        }
        buf_free(&toc);
        free(toc_path);
    }

    char pack_rel[256];
    char *pack_dir = NULL;
    if (ok) {
        snprintf(pack_rel, sizeof(pack_rel), "%s\\%s",
                 opts->schema->unpack_folder, part->pack);
        pack_dir = path_join(opts->out_root, pack_rel);
        if (pack_dir == NULL) {
            err_set(e, "out of memory building the output folder path");
            ok = 0;
        }
    }
    if (ok) {
        ok = finish_plans(part, &bin, plans, count, pack_dir, opts->write_files, e);
    }

    if (ok) {
        emit_log(job, "info", "%s: %lld entries", bin_name, (long long)count);

        part_work work;
        memset(&work, 0, sizeof(work));
        work.job = job;
        work.opts = opts;
        work.part = part;
        work.bin = &bin;
        work.bin_name = bin_name;
        work.pack_dir = pack_dir;
        work.plans = plans;
        work.count = count;
        work.total_bytes = total_bytes;
        work.done_bytes = done_bytes;
        err_clear(&work.first_error);
        InitializeCriticalSection(&work.error_lock);

        unsigned cores = cpu_count();
        if (cores > LEGACY_MAX_THREADS) {
            cores = LEGACY_MAX_THREADS;
        }
        if (cores == 0) {
            cores = 1;
        }
        if ((int64_t)cores > count) {
            cores = (unsigned)(count > 0 ? count : 1);
        }

        HANDLE threads[LEGACY_MAX_THREADS];
        unsigned started = 0;
        for (unsigned t = 0; t + 1 < cores; t++) {
            HANDLE handle = CreateThread(NULL, 0, part_worker, &work, 0, NULL);
            if (handle == NULL) {
                break;
            }
            threads[started++] = handle;
        }
        part_worker(&work);
        for (unsigned t = 0; t < started; t++) {
            WaitForSingleObject(threads[t], INFINITE);
            CloseHandle(threads[t]);
        }

        if (work.failed) {
            *e = work.first_error;
            if (!e->set) {
                err_set(e, "unpack failed in %s", part->container);
            }
            ok = 0;
        }
        DeleteCriticalSection(&work.error_lock);

        stats->entries_seen += seen;
        stats->skipped += skipped;
        stats->files_written += work.files_written;
        stats->nested_written += work.nested_written;
    }

    if (ok) {
        for (int64_t i = 0; i < count; i++) {
            entry_plan *plan = &plans[i];
            char key[768];
            char rel_slash[512];
            snprintf(rel_slash, sizeof(rel_slash), "%s", plan->rel);
            path_to_slash(rel_slash);
            snprintf(key, sizeof(key), "%s/%s/%s",
                     opts->schema->unpack_folder, part->pack, rel_slash);
            manifest_record(manifest, key, idx_marker, plan->toc_off, 0, bin_name,
                            plan->slot_index, plan->unpacked_size,
                            plan->ext == NULL ? ".bin" : plan->ext,
                            plan->name, plan->rate, plan->channels,
                            plan->lzp2 ? "lzp2" : NULL, plan->lzp2_version,
                            plan->lzp2_chunk);
        }
    }

    free(pack_dir);
    free_plans(plans, count);
    mapped_close(&bin);
    free(bin_path);
    return ok;
}

static int pad_to_align(FILE *sink, int64_t *written, int64_t align) {
    if (align <= 1) {
        return 1;
    }
    int64_t pad = (-*written) % align;
    if (pad < 0) {
        pad += align;
    }
    static const unsigned char zeros[LEGACY_SECTOR] = {0};
    if (pad > 0) {
        if (fwrite(zeros, 1, (size_t)pad, sink) != (size_t)pad) {
            return 0;
        }
        *written += pad;
    }
    return 1;
}

static int copy_into(FILE *sink, const char *path, int64_t *written, err *e) {
    buf data;
    buf_init(&data);
    if (!file_read_all(path, &data)) {
        buf_free(&data);
        err_set(e, "couldnt read %s", path);
        return 0;
    }
    int ok = data.len == 0 || fwrite(data.data, 1, data.len, sink) == data.len;
    if (!ok) {
        err_set(e, "couldnt write the bytes of %s", path);
    } else {
        *written += (int64_t)data.len;
    }
    buf_free(&data);
    return ok;
}

static int rebuild_self_table(job_ctx *job, const legacy_part *part, const char *original_path,
                              const char *folder, const char *out_path,
                              legacy_rebuilt *record, err *e) {
    buf original;
    buf_init(&original);
    if (!file_read_all(original_path, &original)) {
        buf_free(&original);
        err_set(e, "couldnt read %s", part->container);
        return 0;
    }
    if (original.len < (size_t)part->toc_offset + 8) {
        buf_free(&original);
        err_set(e, "%s is too small to hold a table", part->container);
        return 0;
    }

    const unsigned char *raw = (const unsigned char *)original.data;
    int64_t header_size = (int64_t)read_u32(raw + part->toc_offset) << 11;
    int64_t capacity = (header_size - part->toc_offset) / part->entry_size;
    if (header_size <= part->toc_offset || (size_t)header_size > original.len || capacity <= 0) {
        buf_free(&original);
        err_set(e, "%s has a header this rebuilder doesnt recognise", part->container);
        return 0;
    }

    char **files = NULL;
    size_t count = 0;
    if (!repack_list_sorted(folder, &files, &count, e)) {
        buf_free(&original);
        return 0;
    }
    if (count == 0) {
        repack_free_sorted(files, count);
        buf_free(&original);
        err_set(e, "%s holds no files to rebuild from", folder);
        return 0;
    }
    if ((int64_t)count > capacity) {
        repack_free_sorted(files, count);
        buf_free(&original);
        err_set(e, "%zu files wont fit in %s's table, which holds %lld",
                count, part->container, (long long)capacity);
        return 0;
    }

    unsigned char *header = (unsigned char *)malloc((size_t)header_size);
    if (header == NULL) {
        repack_free_sorted(files, count);
        buf_free(&original);
        err_set(e, "out of memory rebuilding %s", part->container);
        return 0;
    }
    memcpy(header, raw, (size_t)header_size);
    buf_free(&original);
    memset(header + part->toc_offset, 0, (size_t)(header_size - part->toc_offset));

    if (!path_make_parent_dirs(out_path)) {
        free(header);
        repack_free_sorted(files, count);
        err_set(e, "couldnt create the folder for %s", out_path);
        return 0;
    }
    FILE *sink = file_open(out_path, "wb");
    if (sink == NULL) {
        free(header);
        repack_free_sorted(files, count);
        err_set(e, "couldnt write %s", out_path);
        return 0;
    }

    int ok = fwrite(header, 1, (size_t)header_size, sink) == (size_t)header_size;
    int64_t written = ok ? header_size : 0;
    if (!ok) {
        err_set(e, "couldnt write the header of %s", out_path);
    }

    for (size_t i = 0; i < count && ok; i++) {
        if (job_cancelled(job)) {
            err_set(e, "Cancelled");
            ok = 0;
            break;
        }
        int64_t align = part->scan_align > 0 ? part->scan_align : LEGACY_SECTOR;
        if (!pad_to_align(sink, &written, align)) {
            err_set(e, "couldnt pad %s", out_path);
            ok = 0;
            break;
        }
        int64_t start = written;
        int64_t size = path_file_size(files[i]);
        if (size < 0) {
            err_set(e, "couldnt size %s", files[i]);
            ok = 0;
            break;
        }
        ok = copy_into(sink, files[i], &written, e);
        if (ok) {
            unsigned char *slot = header + part->toc_offset + i * (size_t)part->entry_size;
            legacy_write_named(part, slot, "Offset", (uint64_t)start);
            legacy_write_named(part, slot, "Original_Size", (uint64_t)size);
        }
        if ((i & 63) == 0) {
            emit_progress(job, (int64_t)i, (int64_t)count, part->pack);
        }
    }

    if (ok) {
        uint32_t n = (uint32_t)count;
        header[4] = (unsigned char)(n & 0xFF);
        header[5] = (unsigned char)((n >> 8) & 0xFF);
        header[6] = (unsigned char)((n >> 16) & 0xFF);
        header[7] = (unsigned char)((n >> 24) & 0xFF);
        if (fseek(sink, 0, SEEK_SET) != 0 ||
            fwrite(header, 1, (size_t)header_size, sink) != (size_t)header_size) {
            err_set(e, "couldnt patch the table of %s", out_path);
            ok = 0;
        }
    }

    if (fclose(sink) != 0 && ok) {
        err_set(e, "couldnt finish writing %s", out_path);
        ok = 0;
    }
    free(header);
    repack_free_sorted(files, count);

    if (ok) {
        snprintf(record->container, sizeof(record->container), "%s", part->pack);
        snprintf(record->out_path, sizeof(record->out_path), "%s", out_path);
        record->entries = (int64_t)count;
        record->bytes = written;
    }
    return ok;
}

static int rebuild_stream_bank(job_ctx *job, const legacy_part *part, const char *original_path,
                               const char *folder, const char *out_path,
                               legacy_rebuilt *record, err *e) {
    char **files = NULL;
    size_t count = 0;
    if (!repack_list_sorted(folder, &files, &count, e)) {
        return 0;
    }
    if (count == 0) {
        repack_free_sorted(files, count);
        err_set(e, "%s holds no streams to rebuild from", folder);
        return 0;
    }
    if (!path_make_parent_dirs(out_path)) {
        repack_free_sorted(files, count);
        err_set(e, "couldnt create the folder for %s", out_path);
        return 0;
    }

    FILE *sink = file_open(out_path, "wb");
    if (sink == NULL) {
        repack_free_sorted(files, count);
        err_set(e, "couldnt write %s", out_path);
        return 0;
    }

    int ok = 1;
    int64_t written = 0;
    if (part->scan_start > 0) {
        buf head;
        buf_init(&head);
        if (!file_read_all(original_path, &head) || (int64_t)head.len < part->scan_start) {
            buf_free(&head);
            fclose(sink);
            repack_free_sorted(files, count);
            err_set(e, "couldnt read the header of %s", part->container);
            return 0;
        }
        ok = fwrite(head.data, 1, (size_t)part->scan_start, sink) == (size_t)part->scan_start;
        if (!ok) {
            err_set(e, "couldnt write the header of %s", out_path);
        }
        written = part->scan_start;
        buf_free(&head);
    }
    for (size_t i = 0; i < count && ok; i++) {
        if (job_cancelled(job)) {
            err_set(e, "Cancelled");
            ok = 0;
            break;
        }
        ok = copy_into(sink, files[i], &written, e) &&
             pad_to_align(sink, &written, part->scan_align);
        if ((i & 127) == 0) {
            emit_progress(job, (int64_t)i, (int64_t)count, part->pack);
        }
    }

    if (fclose(sink) != 0 && ok) {
        err_set(e, "couldnt finish writing %s", out_path);
        ok = 0;
    }
    repack_free_sorted(files, count);

    if (ok) {
        snprintf(record->container, sizeof(record->container), "%s", part->pack);
        snprintf(record->out_path, sizeof(record->out_path), "%s", out_path);
        record->entries = (int64_t)count;
        record->bytes = written;
    }
    return ok;
}


#define BANK_DATA_ALIGN 16

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} bank_buf;

static int bank_buf_reserve(bank_buf *b, size_t need) {
    if (need <= b->cap) {
        return 1;
    }
    size_t cap = b->cap == 0 ? (1u << 20) : b->cap;
    while (cap < need) {
        cap *= 2;
    }
    unsigned char *grown = (unsigned char *)realloc(b->data, cap);
    if (grown == NULL) {
        free(b->data);
        b->data = NULL;
        b->cap = 0;
        b->len = 0;
        return 0;
    }
    b->data = grown;
    b->cap = cap;
    return 1;
}

static int rebuild_sound_banks(job_ctx *job, const legacy_part *part,
                               const char *bin_path, const char *toc_path,
                               const char *folder, const char *out_bin,
                               const char *out_toc, legacy_rebuilt *record, err *e) {
    buf toc;
    buf_init(&toc);
    if (!file_read_all(toc_path, &toc)) {
        buf_free(&toc);
        err_set(e, "couldnt read %s", part->toc);
        return 0;
    }
    unsigned char *table = (unsigned char *)toc.data;

    mapped src;
    if (!mapped_open(&src, bin_path, e)) {
        buf_free(&toc);
        return 0;
    }

    char **files = NULL;
    size_t count = 0;
    if (!repack_list_sorted(folder, &files, &count, e)) {
        mapped_close(&src);
        buf_free(&toc);
        return 0;
    }

    if (!path_make_parent_dirs(out_bin) || !path_make_parent_dirs(out_toc)) {
        repack_free_sorted(files, count);
        mapped_close(&src);
        buf_free(&toc);
        err_set(e, "couldnt create the folder for %s", out_bin);
        return 0;
    }
    FILE *sink = file_open(out_bin, "wb");
    if (sink == NULL) {
        repack_free_sorted(files, count);
        mapped_close(&src);
        buf_free(&toc);
        err_set(e, "couldnt write %s", out_bin);
        return 0;
    }

    bank_buf work;
    memset(&work, 0, sizeof(work));

    int ok = 1;
    int64_t written = 0;
    int64_t read_start = 0;
    int64_t used = 0;
    int64_t moved = 0;
    int banks = 0;
    size_t at = 0;

    while (ok && at + 12 <= toc.len) {
        uint32_t declared = read_u32(table + at + 4);
        uint32_t first = read_u32(table + at + 8);
        if (read_u32(table + at) == 0 && declared == 0 && first == 0) {
            break;
        }
        if (first < 12 || (first - 8) % 4 != 0) {
            break;
        }
        int64_t n = (int64_t)((first - 8) / 4);
        if (n <= 0 || at + first > toc.len) {
            break;
        }
        if (read_start + declared > src.size) {
            err_set(e, "%s bank %d runs past the container", part->pack, banks);
            ok = 0;
            break;
        }

        if (!bank_buf_reserve(&work, declared)) {
            err_set(e, "out of memory holding a bank of %u bytes", declared);
            ok = 0;
            break;
        }
        memcpy(work.data, src.base + read_start, declared);
        work.len = declared;

        int64_t furthest = 0;
        for (int64_t i = 0; i < n && ok; i++) {
            size_t rec = at + read_u32(table + at + 8 + (size_t)i * 4);
            if (rec + 40 > toc.len) {
                break;
            }
            if ((int64_t)rec > furthest) {
                furthest = (int64_t)rec;
            }
            uint32_t align = read_u32(table + rec + 8) & 0xFFFF;
            uint32_t samples = read_u32(table + rec + 16);
            uint32_t where = read_u32(table + rec + 20);
            uint32_t blen = read_u32(table + rec + 24);
            if (align == 0 || blen != samples * align || where + blen > declared) {
                continue;
            }
            if (used >= (int64_t)count) {
                err_set(e, "%s holds fewer files than the table has sounds", folder);
                ok = 0;
                break;
            }

            buf chunk;
            buf_init(&chunk);
            if (!file_read_all(files[used], &chunk)) {
                err_set(e, "couldnt read %s", files[used]);
                buf_free(&chunk);
                ok = 0;
                break;
            }
            size_t usable = chunk.len - (chunk.len % align);

            if (usable <= blen) {
                if (usable > 0) {
                    memcpy(work.data + where, chunk.data, usable);
                }
            } else {
                size_t landing = work.len;
                landing += (BANK_DATA_ALIGN - (landing % BANK_DATA_ALIGN)) % BANK_DATA_ALIGN;
                if (!bank_buf_reserve(&work, landing + usable)) {
                    err_set(e, "out of memory growing a bank for %s", files[used]);
                    buf_free(&chunk);
                    ok = 0;
                    break;
                }
                if (landing > work.len) {
                    memset(work.data + work.len, 0, landing - work.len);
                }
                memcpy(work.data + landing, chunk.data, usable);
                work.len = landing + usable;
                put_u32(table + rec + 20, (uint32_t)landing);
                moved++;
            }
            put_u32(table + rec + 16, (uint32_t)(usable / align));
            put_u32(table + rec + 24, (uint32_t)usable);
            buf_free(&chunk);
            used++;
        }

        if (ok) {
            if (work.len > 0 && fwrite(work.data, 1, work.len, sink) != work.len) {
                err_set(e, "couldnt write bank %d of %s", banks, out_bin);
                ok = 0;
                break;
            }
            written += (int64_t)work.len;
            put_u32(table + at + 4, (uint32_t)work.len);
            if (!pad_to_align(sink, &written, LEGACY_SECTOR)) {
                err_set(e, "couldnt pad %s", out_bin);
                ok = 0;
                break;
            }
            read_start = (read_start + declared + LEGACY_SECTOR - 1) /
                         LEGACY_SECTOR * LEGACY_SECTOR;
            banks++;
            at = (size_t)((furthest + 40 + LEGACY_SECTOR - 1) / LEGACY_SECTOR * LEGACY_SECTOR);
        }
    }

    if (ok && used != (int64_t)count) {
        emit_log(job, "warn", "%s holds %llu files but the table addresses %lld",
                 part->pack, (unsigned long long)count, (long long)used);
    }
    if (fclose(sink) != 0 && ok) {
        err_set(e, "couldnt finish writing %s", out_bin);
        ok = 0;
    }
    if (ok && !file_write_all(out_toc, toc.data, toc.len)) {
        err_set(e, "couldnt write %s", out_toc);
        ok = 0;
    }

    free(work.data);
    repack_free_sorted(files, count);
    mapped_close(&src);
    buf_free(&toc);

    if (ok) {
        snprintf(record->container, sizeof(record->container), "%s", part->pack);
        snprintf(record->out_path, sizeof(record->out_path), "%s", out_bin);
        record->entries = used;
        record->bytes = written;
        emit_log(job, "info", "%s rebuilt: %d banks, %lld sounds, %lld relocated",
                 part->pack, banks, (long long)used, (long long)moved);
    }
    return ok;
}

static int restore_lzp2(const unsigned char *original, size_t original_len,
                        buf *chunk, err *e) {
    if (!lzp2_looks_like(original, original_len)) {
        return 1;
    }
    buf work;
    buf_init(&work);
    err quiet = {0};
    int same = lzp2_decompress_chain(original, original_len, &work, &quiet) &&
               work.len == chunk->len &&
               memcmp(work.data, chunk->data, chunk->len) == 0;
    int ok;
    if (same) {
        buf_reset(chunk);
        ok = buf_put(chunk, original, original_len);
    } else {
        buf_reset(&work);
        if (lzp2_compress_chain((const unsigned char *)chunk->data, chunk->len,
                                lzp2_version(original, original_len),
                                lzp2_chain_chunk(original, original_len),
                                &work, &quiet)) {
            buf_reset(chunk);
            ok = buf_put(chunk, (const unsigned char *)work.data, work.len);
        } else {
            err_set(e, "couldnt recompress an edited LZP2 entry: %s",
                    quiet.set ? quiet.text : "unknown");
            ok = 0;
        }
    }
    buf_free(&work);
    if (!ok && !e->set) {
        err_set(e, "out of memory restoring an LZP2 entry");
    }
    return ok;
}

static char *numbered_source(char **files, size_t count, int64_t index) {
    char stem[32];
    int n = snprintf(stem, sizeof(stem), "entry_%05lld", (long long)index);
    if (n <= 0 || (size_t)n >= sizeof(stem)) {
        return NULL;
    }
    for (size_t k = 0; k < count; k++) {
        const char *slash = strrchr(files[k], '\\');
        const char *base = slash == NULL ? files[k] : slash + 1;
        if (_strnicmp(base, stem, (size_t)n) == 0 &&
            (base[n] == 0 || base[n] == '.')) {
            return _strdup(files[k]);
        }
    }
    return NULL;
}

static char *unique_row_name(char **used, int64_t used_count, char *safe, int64_t row) {
    for (int64_t k = 0; k < used_count; k++) {
        if (_stricmp(used[k], safe) != 0) {
            continue;
        }
        const char *dot = strrchr(safe, '.');
        const char *slash = strrchr(safe, '\\');
        if (dot != NULL && slash != NULL && dot < slash) {
            dot = NULL;
        }
        size_t stem = dot == NULL ? strlen(safe) : (size_t)(dot - safe);
        size_t room = strlen(safe) + 24;
        char *unique = (char *)malloc(room);
        if (unique == NULL) {
            return safe;
        }
        snprintf(unique, room, "%.*s__%05lld%s", (int)stem, safe, (long long)row, dot == NULL ? "" : dot);
        free(safe);
        return unique;
    }
    return safe;
}

static int *accum_own_flags(const legacy_part *part, const unsigned char *table, int64_t capacity,
                            int64_t src_size, const name_list *names) {
    int *own = (int *)calloc((size_t)(capacity > 0 ? capacity : 1), sizeof(int));
    char **rel = (char **)calloc((size_t)(capacity > 0 ? capacity : 1), sizeof(char *));
    int64_t *row = (int64_t *)calloc((size_t)(capacity > 0 ? capacity : 1), sizeof(int64_t));
    int *marks = (int *)calloc((size_t)(capacity > 0 ? capacity : 1), sizeof(int));
    if (own == NULL || rel == NULL || row == NULL || marks == NULL) {
        free(own);
        free(rel);
        free(row);
        free(marks);
        return NULL;
    }
    int64_t kept = 0;
    int64_t read_at = 0;
    for (int64_t i = 0; i < capacity && read_at < src_size; i++) {
        const unsigned char *raw = table + (size_t)i * (size_t)part->entry_size;
        uint64_t span = legacy_read_named(part, raw, "Original_Size");
        if (span == 0) {
            span = legacy_read_named(part, raw, "Fallback_Size");
        }
        if (span == 0) {
            continue;
        }
        int64_t step = ((int64_t)span + LEGACY_SECTOR - 1) / LEGACY_SECTOR * LEGACY_SECTOR;
        if (read_at + (int64_t)span <= src_size) {
            const char *nm = name_at(names, i);
            char *safe = nm != NULL ? path_sanitize_relative(nm) : NULL;
            if (safe != NULL) {
                rel[kept] = unique_row_name(rel, kept, safe, i);
                row[kept] = i;
                kept++;
            }
        }
        read_at += step;
    }
    if (subfolder_mark_shared(rel, kept, marks)) {
        for (int64_t k = 0; k < kept; k++) {
            own[row[k]] = marks[k];
        }
    }
    for (int64_t k = 0; k < kept; k++) {
        free(rel[k]);
    }
    free(rel);
    free(row);
    free(marks);
    return own;
}

static int rebuild_accum(job_ctx *job, const legacy_part *part, const char *game_id,
                         const char *ref_dir, const char *bin_path,
                         const char *toc_path, const char *folder,
                         const char *out_bin, const char *out_toc,
                         legacy_rebuilt *record, err *e) {
    buf toc;
    buf_init(&toc);
    if (!file_read_all(toc_path, &toc)) {
        buf_free(&toc);
        err_set(e, "couldnt read %s", part->toc);
        return 0;
    }
    unsigned char *table = (unsigned char *)toc.data;

    mapped src;
    if (!mapped_open(&src, bin_path, e)) {
        buf_free(&toc);
        return 0;
    }

    name_list names;
    int have_names = 0;
    if (part->named) {
        have_names = name_list_load(&names, ref_dir, game_id, part->pack);
    }
    char **files = NULL;
    size_t count = 0;
    if (!repack_list_sorted(folder, &files, &count, e)) {
        if (have_names) {
            name_list_free(&names);
        }
        mapped_close(&src);
        buf_free(&toc);
        return 0;
    }
    if (!path_make_parent_dirs(out_bin) || !path_make_parent_dirs(out_toc)) {
        if (have_names) {
            name_list_free(&names);
        }
        repack_free_sorted(files, count);
        mapped_close(&src);
        buf_free(&toc);
        err_set(e, "couldnt create the folder for %s", out_bin);
        return 0;
    }
    FILE *sink = file_open(out_bin, "wb");
    if (sink == NULL) {
        if (have_names) {
            name_list_free(&names);
        }
        repack_free_sorted(files, count);
        mapped_close(&src);
        buf_free(&toc);
        err_set(e, "couldnt write %s", out_bin);
        return 0;
    }

    int64_t capacity = (int64_t)(toc.len / (size_t)part->entry_size);
    if (part->row_count > 0 && part->row_count < capacity) {
        capacity = part->row_count;
    }
    buf chunks;
    buf_init(&chunks);
    int64_t written = 0;
    int64_t read_at = 0;
    int64_t used = 0;
    int ok = 1;
    char **used_names = (char **)calloc((size_t)(capacity > 0 ? capacity : 1),
                                        sizeof(char *));
    int64_t seen_names = 0;
    if (used_names == NULL) {
        fclose(sink);
        if (have_names) {
            name_list_free(&names);
        }
        repack_free_sorted(files, count);
        mapped_close(&src);
        buf_free(&toc);
        err_set(e, "out of memory tracking names");
        return 0;
    }

    int *own_flags = have_names ? accum_own_flags(part, table, capacity, src.size, &names) : NULL;
    for (int64_t i = 0; i < capacity && ok && read_at < src.size; i++) {
        unsigned char *raw = table + (size_t)i * (size_t)part->entry_size;
        uint64_t span = legacy_read_named(part, raw, "Original_Size");
        const char *field = "Original_Size";
        if (span == 0) {
            span = legacy_read_named(part, raw, "Fallback_Size");
            field = "Fallback_Size";
        }
        if (span == 0) {
            continue;
        }
        int64_t old_step = ((int64_t)span + LEGACY_SECTOR - 1) / LEGACY_SECTOR * LEGACY_SECTOR;
        if (read_at + (int64_t)span > src.size) {
            read_at += old_step;
            continue;
        }
        char *source = NULL;
        if (have_names) {
            const char *nm = name_at(&names, i);
            char *safe = nm != NULL ? path_sanitize_relative(nm) : NULL;
            if (safe != NULL) {
                safe = unique_row_name(used_names, seen_names, safe, i);
                if (seen_names < (int64_t)capacity) {
                    used_names[seen_names++] = safe;
                    source = path_join(folder, safe);
                } else {
                    source = path_join(folder, safe);
                    free(safe);
                }
            } else {
                source = numbered_source(files, count, used);
            }
        } else if (used < (int64_t)count) {
            source = _strdup(files[used]);
        }
        if (source == NULL) {
            if (have_names) {
                err_set(e, "%s has no file for entry %lld, which the list leaves "
                           "unnamed, the unpack writes those as entry_%05lld.*",
                        folder, (long long)i, (long long)used);
            } else {
                err_set(e, "%s holds %zu files at the top level but the table "
                           "needs at least %lld; if the unpack used a name list, "
                           "give the rebuild the same one",
                        folder, count, (long long)used + 1);
            }
            ok = 0;
            break;
        }

        buf chunk;
        buf_init(&chunk);
        int read_ok = own_flags != NULL ? repack_read_chunk_at(source, own_flags[i], &chunk, e)
                                        : repack_read_chunk(source, &chunk, e);
        if (read_ok && part->chunk_table && have_names) {
            read_ok = wozindex_refresh(folder, source, &chunk, e);
        }
        if (!read_ok) {
            free(source);
            buf_free(&chunk);
            ok = 0;
            break;
        }
        free(source);
        size_t plain_len = chunk.len;
        int woz_packed = part->chunk_table && strcmp(field, "Original_Size") == 0;
        if (woz_packed) {
            if (!woz_pack_entry(src.base + read_at, (size_t)span, &chunk, e)) {
                buf_free(&chunk);
                ok = 0;
                break;
            }
        } else if (!restore_lzp2(src.base + read_at, (size_t)span, &chunk, e)) {
            buf_free(&chunk);
            ok = 0;
            break;
        }
        if (chunk.len > 0 && fwrite(chunk.data, 1, chunk.len, sink) != chunk.len) {
            err_set(e, "couldnt write entry %lld into %s", (long long)i, out_bin);
            buf_free(&chunk);
            ok = 0;
            break;
        }
        written += (int64_t)chunk.len;

        int64_t new_step = ((int64_t)chunk.len + LEGACY_SECTOR - 1) /
                           LEGACY_SECTOR * LEGACY_SECTOR;
        int64_t pad = new_step - (int64_t)chunk.len;
        if (pad > 0) {
            int64_t have = old_step - (int64_t)span;
            if (have > pad) {
                have = pad;
            }
            if (have > 0 && read_at + (int64_t)span + have <= src.size) {
                if (fwrite(src.base + read_at + span, 1, (size_t)have, sink) != (size_t)have) {
                    err_set(e, "couldnt carry the padding of entry %lld", (long long)i);
                    buf_free(&chunk);
                    ok = 0;
                    break;
                }
                written += have;
            } else {
                have = 0;
            }
            for (int64_t k = have; k < pad && ok; k++) {
                if (fputc(0, sink) == EOF) {
                    err_set(e, "couldnt pad %s", out_bin);
                    ok = 0;
                }
            }
            if (ok) {
                written += pad - have;
            }
        }

        if (ok && woz_packed) {
            uint64_t offset = (uint64_t)chunks.len;
            if (!woz_chunk_list((const unsigned char *)chunk.data, chunk.len, &chunks, e)) {
                ok = 0;
            } else if (plain_len > 0xFFFFFFFFu || new_step > 0xFFFFFFFFLL || offset > 0xFFFFFFFFu) {
                err_set(e, "entry %lld of %s is too big for its table row", (long long)i, part->container);
                ok = 0;
            } else {
                legacy_write_named(part, raw, "Fallback_Size", (uint64_t)plain_len);
                legacy_write_named(part, raw, "Original_Size", (uint64_t)new_step);
                legacy_write_named(part, raw, "Chunk_Offset", offset);
            }
        } else if (ok) {
            legacy_write_named(part, raw, field, (uint64_t)chunk.len);
        }
        buf_free(&chunk);
        read_at += old_step;
        used++;
    }

    if (fclose(sink) != 0 && ok) {
        err_set(e, "couldnt finish writing %s", out_bin);
        ok = 0;
    }
    if (ok && part->chunk_table) {
        size_t rows = (size_t)capacity * (size_t)part->entry_size;
        buf whole;
        buf_init(&whole);
        if (!buf_put(&whole, toc.data, rows) ||
            (chunks.len > 0 && !buf_put(&whole, chunks.data, chunks.len))) {
            err_set(e, "out of memory building %s", out_toc);
            ok = 0;
        } else if (!file_write_all(out_toc, whole.data, whole.len)) {
            err_set(e, "couldnt write %s", out_toc);
            ok = 0;
        }
        buf_free(&whole);
    } else if (ok && !file_write_all(out_toc, toc.data, toc.len)) {
        err_set(e, "couldnt write %s", out_toc);
        ok = 0;
    }
    buf_free(&chunks);

    for (int64_t k = 0; k < seen_names; k++) {
        free(used_names[k]);
    }
    free(used_names);
    free(own_flags);
    if (have_names) {
        name_list_free(&names);
    }
    repack_free_sorted(files, count);
    mapped_close(&src);
    buf_free(&toc);

    if (ok) {
        snprintf(record->container, sizeof(record->container), "%s", part->pack);
        snprintf(record->out_path, sizeof(record->out_path), "%s", out_bin);
        record->entries = used;
        record->bytes = written;
        emit_log(job, "info", "%s rebuilt: %lld entries, %lld bytes",
                 part->pack, (long long)used, (long long)written);
    }
    return ok;
}

int legacy_rebuild(job_ctx *job, const char *game_id, const char *base_dir,
                   const char *src_root, const char *unpack_folder,
                   const char *out_dir, const char *ref_dir,
                   legacy_rebuild_stats *stats, err *e) {
    memset(stats, 0, sizeof(*stats));
    codec_init();
    {
        const game_schema *s = schema_find(game_id);
        codec_set_big_endian(s != NULL && !s->little_endian);
    }

    int part_total = 0;
    legacy_part_set found;
    const legacy_part *parts = legacy_parts_in(job, base_dir, game_id, &part_total, &found);
    if (parts == NULL) {
        err_set(e, "%s has no rebuildable containers", game_id == NULL ? "?" : game_id);
        return 0;
    }

    for (int i = 0; i < part_total; i++) {
        const legacy_part *part = &parts[i];
        if (part->appendable) {
            continue;
        }
        if (stats->count >= LEGACY_PART_MAX) {
            err_set(e, "%s has more rebuildable containers than the report holds", game_id);
            return 0;
        }

        char rel[256];
        snprintf(rel, sizeof(rel), "%s\\%s", unpack_folder, part->pack);
        char *folder = path_join(src_root, rel);
        if (folder == NULL) {
            err_set(e, "out of memory locating the %s folder", part->pack);
            return 0;
        }
        if (!path_is_dir(folder)) {
            emit_log(job, "warn", "%s has not been unpacked yet, skipping it", part->pack);
            free(folder);
            continue;
        }

        const char *leaf = strrchr(part->container, '\\');
        leaf = leaf == NULL ? part->container : leaf + 1;
        char *out_path = path_join(out_dir, part->container);
        if (out_path == NULL) {
            free(folder);
            err_set(e, "out of memory building the output path for %s", leaf);
            return 0;
        }

        emit_log(job, "info", "rebuilding %s from %s", leaf, part->pack);

        int ok;
        if (part->table == LEGACY_TABLE_ACCUM) {
            char *source = path_join(base_dir, part->container);
            char *original = path_join(base_dir, part->toc);
            char *out_toc = path_join(out_dir, part->toc);
            if (source == NULL || original == NULL || out_toc == NULL) {
                free(source);
                free(original);
                free(out_toc);
                free(folder);
                free(out_path);
                err_set(e, "out of memory locating %s", leaf);
                return 0;
            }
            ok = rebuild_accum(job, part, game_id, ref_dir, source, original, folder,
                               out_path, out_toc, &stats->items[stats->count], e);
            free(source);
            free(original);
            free(out_toc);
        } else if (part->table == LEGACY_TABLE_BANKS) {
            char *original = path_join(base_dir, part->toc);
            char *out_toc = path_join(out_dir, part->toc);
            if (original == NULL || out_toc == NULL) {
                free(original);
                free(out_toc);
                free(folder);
                free(out_path);
                err_set(e, "out of memory locating %s", leaf);
                return 0;
            }
            char *source = path_join(base_dir, part->container);
            if (source == NULL) {
                free(original);
                free(out_toc);
                free(folder);
                free(out_path);
                err_set(e, "out of memory locating %s", leaf);
                return 0;
            }
            ok = rebuild_sound_banks(job, part, source, original, folder, out_path,
                                     out_toc, &stats->items[stats->count], e);
            free(source);
            free(original);
            free(out_toc);
        } else if (part->table == LEGACY_TABLE_SELF) {
            char *original = path_join(base_dir, part->container);
            if (original == NULL) {
                free(folder);
                free(out_path);
                err_set(e, "out of memory locating %s", leaf);
                return 0;
            }
            ok = rebuild_self_table(job, part, original, folder, out_path,
                                    &stats->items[stats->count], e);
            free(original);
        } else {
            char *original = path_join(base_dir, part->container);
            if (original == NULL) {
                free(folder);
                free(out_path);
                err_set(e, "out of memory locating %s", leaf);
                return 0;
            }
            ok = rebuild_stream_bank(job, part, original, folder, out_path,
                                     &stats->items[stats->count], e);
            free(original);
        }

        free(folder);
        free(out_path);
        if (!ok) {
            return 0;
        }
        stats->count++;
    }

    if (stats->count == 0) {
        err_set(e, "nothing to rebuild: unpack the game first");
        return 0;
    }
    emit_progress(job, 1, 1, "done");
    return 1;
}

int legacy_run(job_ctx *job, const unpack_opts *opts, unpack_stats *stats,
             manifest_writer *manifest, err *e) {
    int part_total = 0;
    legacy_part_set found;
    const legacy_part *parts = legacy_parts_in(job, opts->base_dir, opts->schema->game_id,
                                               &part_total, &found);
    if (parts == NULL) {
        err_set(e, "%s has no container list", opts->schema->game_id);
        return 0;
    }

    int64_t total_bytes = 0;
    for (int i = 0; i < part_total; i++) {
        char *path = path_join(opts->base_dir, parts[i].container);
        if (path == NULL) {
            continue;
        }
        if (parts[i].table == LEGACY_TABLE_NONE) {
            int64_t size = path_file_size(path);
            total_bytes += size > 0 ? size : 0;
        } else if (parts[i].table == LEGACY_TABLE_SELF) {
            buf head;
            buf_init(&head);
            if (file_read_all(path, &head)) {
                int64_t declared = head.len >= (size_t)parts[i].count_offset + 4
                                       ? (int64_t)read_u32((const unsigned char *)head.data + parts[i].count_offset)
                                       : 0;
                for (int64_t k = 0; k < declared; k++) {
                    size_t at = (size_t)parts[i].toc_offset + (size_t)k * (size_t)parts[i].entry_size;
                    if (at + (size_t)parts[i].entry_size > head.len) {
                        break;
                    }
                    total_bytes += (int64_t)legacy_read_named(
                        &parts[i], (const unsigned char *)head.data + at, "Original_Size");
                }
            }
            buf_free(&head);
        } else if (parts[i].table == LEGACY_TABLE_ACCUM) {
            int64_t size = path_file_size(path);
            total_bytes += size > 0 ? size : 0;
        } else {
            char *toc_path = path_join(opts->base_dir, parts[i].toc);
            if (toc_path != NULL) {
                buf toc;
                buf_init(&toc);
                if (file_read_all(toc_path, &toc)) {
                    int64_t declared = toc.len >= (size_t)parts[i].count_offset + 4
                                           ? (int64_t)read_u32((const unsigned char *)toc.data + parts[i].count_offset)
                                           : 0;
                    for (int64_t k = 0; k < declared; k++) {
                        size_t at = (size_t)parts[i].toc_offset + (size_t)k * (size_t)parts[i].entry_size;
                        if (at + (size_t)parts[i].entry_size > toc.len) {
                            break;
                        }
                        total_bytes += (int64_t)legacy_read_named(
                            &parts[i], (const unsigned char *)toc.data + at, "Original_Size");
                    }
                }
                buf_free(&toc);
                free(toc_path);
            }
        }
        free(path);
    }
    if (total_bytes <= 0) {
        total_bytes = 1;
    }

    volatile LONG64 done_bytes = 0;
    int ok = 1;
    for (int i = 0; i < part_total && ok; i++) {
        if (job_cancelled(job)) {
            err_set(e, "Cancelled");
            ok = 0;
            break;
        }
        name_list names;
        int have_names = 0;
        if (parts[i].named) {
            have_names = name_list_load(&names, opts->ref_dir,
                                        opts->schema->game_id, parts[i].pack);
            if (have_names) {
                emit_log(job, "info", "%s: %lld filenames loaded, entries keep their real paths",
                         parts[i].pack, (long long)names.count);
            } else {
                emit_log(job, "warn",
                         "%s: no name list in the Filenames folder, entries will be numbered",
                         parts[i].pack);
            }
        }
        ok = run_part(job, opts, i, &parts[i], have_names ? &names : NULL,
                      manifest, stats, total_bytes, &done_bytes, e);
        if (have_names) {
            name_list_free(&names);
        }
    }

    if (ok) {
        emit_progress(job, total_bytes, total_bytes, "done");
    }
    return ok;
}
