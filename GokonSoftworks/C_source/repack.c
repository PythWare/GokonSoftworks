#define WIN32_LEAN_AND_MEAN
#include "repack.h"
#include "zp1.h"
#include "btsc.h"
#include "codec.h"
#include "legacy.h"
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned char *data;
    size_t len;
} chunk_ref;

typedef struct {
    chunk_ref *items;
    size_t count;
    size_t cap;
} chunk_list;

static void chunk_list_init(chunk_list *list) {
    memset(list, 0, sizeof(*list));
}

static void chunk_list_free(chunk_list *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].data);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int chunk_list_push(chunk_list *list, unsigned char *data, size_t len) {
    if (list->count == list->cap) {
        size_t cap = list->cap == 0 ? 8 : list->cap * 2;
        chunk_ref *grown = (chunk_ref *)realloc(list->items, sizeof(chunk_ref) * cap);
        if (grown == NULL) {
            free(data);
            return 0;
        }
        list->items = grown;
        list->cap = cap;
    }
    list->items[list->count].data = data;
    list->items[list->count].len = len;
    list->count++;
    return 1;
}

static int chunk_list_take_buf(chunk_list *list, buf *b) {
    unsigned char *copy = NULL;
    if (b->len > 0) {
        copy = (unsigned char *)malloc(b->len);
        if (copy == NULL) {
            return 0;
        }
        memcpy(copy, b->data, b->len);
    }
    return chunk_list_push(list, copy, b->len);
}

typedef struct {
    char *path;
    char stem[192];
    long long number;
    int has_number;
} folder_file;

static int compare_folder_file(const void *left, const void *right) {
    const folder_file *a = (const folder_file *)left;
    const folder_file *b = (const folder_file *)right;

    int a_rank = a->has_number ? 0 : 1;
    int b_rank = b->has_number ? 0 : 1;
    if (a_rank != b_rank) {
        return a_rank < b_rank ? -1 : 1;
    }
    if (a_rank == 0 && a->number != b->number) {
        return a->number < b->number ? -1 : 1;
    }
    return strcmp(a->stem, b->stem);
}

static void lower_ascii(char *text) {
    for (; *text; text++) {
        if (*text >= 'A' && *text <= 'Z') {
            *text = (char)(*text - 'A' + 'a');
        }
    }
}

static void fill_sort_key(folder_file *entry, const char *name) {
    const char *dot = strrchr(name, '.');
    size_t stem_len = dot == NULL ? strlen(name) : (size_t)(dot - name);
    if (stem_len >= sizeof(entry->stem)) {
        stem_len = sizeof(entry->stem) - 1;
    }
    memcpy(entry->stem, name, stem_len);
    entry->stem[stem_len] = 0;
    lower_ascii(entry->stem);

    entry->has_number = 0;
    entry->number = 0;
    const char *scan = entry->stem;
    while (*scan) {
        if (*scan >= '0' && *scan <= '9') {
            long long value = 0;
            while (*scan >= '0' && *scan <= '9') {
                value = value * 10 + (*scan - '0');
                scan++;
            }
            entry->number = value;
            entry->has_number = 1;
            continue;
        }
        scan++;
    }
}

static void folder_files_free(folder_file *files, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(files[i].path);
    }
    free(files);
}

static int list_folder_files(const char *folder, folder_file **out, size_t *count_out, err *e) {
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", folder);
    wchar_t *wide = path_to_wide(pattern);
    if (wide == NULL) {
        err_set(e, "out of memory listing %s", folder);
        return 0;
    }

    WIN32_FIND_DATAW found;
    HANDLE handle = FindFirstFileW(wide, &found);
    free(wide);
    if (handle == INVALID_HANDLE_VALUE) {
        err_set(e, "couldnt list %s", folder);
        return 0;
    }

    folder_file *files = NULL;
    size_t count = 0;
    size_t cap = 0;
    int ok = 1;

    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        char *name = wide_to_utf8(found.cFileName);
        if (name == NULL) {
            ok = 0;
            break;
        }
        if (count == cap) {
            size_t grown = cap == 0 ? 16 : cap * 2;
            folder_file *bigger = (folder_file *)realloc(files, sizeof(folder_file) * grown);
            if (bigger == NULL) {
                free(name);
                ok = 0;
                break;
            }
            files = bigger;
            cap = grown;
        }
        memset(&files[count], 0, sizeof(folder_file));
        files[count].path = path_join(folder, name);
        if (files[count].path == NULL) {
            free(name);
            ok = 0;
            break;
        }
        fill_sort_key(&files[count], name);
        free(name);
        count++;
    } while (FindNextFileW(handle, &found));

    FindClose(handle);

    if (!ok) {
        folder_files_free(files, count);
        err_set(e, "out of memory listing %s", folder);
        return 0;
    }

    qsort(files, count, sizeof(folder_file), compare_folder_file);
    *out = files;
    *count_out = count;
    return 1;
}

int repack_list_sorted(const char *folder, char ***paths_out, size_t *count_out, err *e) {
    folder_file *files = NULL;
    size_t count = 0;
    if (!list_folder_files(folder, &files, &count, e)) {
        return 0;
    }
    char **paths = (char **)calloc(count == 0 ? 1 : count, sizeof(char *));
    if (paths == NULL) {
        folder_files_free(files, count);
        err_set(e, "out of memory listing %s", folder);
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        paths[i] = files[i].path;
        files[i].path = NULL;
    }
    folder_files_free(files, count);
    *paths_out = paths;
    *count_out = count;
    return 1;
}

void repack_free_sorted(char **paths, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(paths[i]);
    }
    free(paths);
}

static int has_extension(const char *path, const char *const *exts, int ext_count) {
    const char *dot = strrchr(path, '.');
    if (dot == NULL) {
        return 0;
    }
    char lowered[32];
    size_t len = strlen(dot);
    if (len >= sizeof(lowered)) {
        return 0;
    }
    memcpy(lowered, dot, len + 1);
    lower_ascii(lowered);
    for (int i = 0; i < ext_count; i++) {
        if (strcmp(lowered, exts[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static size_t align_up_to(size_t value, size_t alignment) {
    return (value + (alignment - 1)) & ~(alignment - 1);
}

static int buf_zeros(buf *b, size_t count) {
    static const unsigned char zeros[256] = {0};
    while (count > 0) {
        size_t take = count > sizeof(zeros) ? sizeof(zeros) : count;
        if (!buf_put(b, zeros, take)) {
            return 0;
        }
        count -= take;
    }
    return 1;
}

static int buf_u32(buf *b, uint32_t value) {
    unsigned char raw[4];
    raw[0] = (unsigned char)(value & 0xFF);
    raw[1] = (unsigned char)((value >> 8) & 0xFF);
    raw[2] = (unsigned char)((value >> 16) & 0xFF);
    raw[3] = (unsigned char)((value >> 24) & 0xFF);
    return buf_put(b, raw, 4);
}

static int buf_word(buf *b, uint32_t value, int big) {
    if (!big) {
        return buf_u32(b, value);
    }
    unsigned char raw[4];
    raw[0] = (unsigned char)((value >> 24) & 0xFF);
    raw[1] = (unsigned char)((value >> 16) & 0xFF);
    raw[2] = (unsigned char)((value >> 8) & 0xFF);
    raw[3] = (unsigned char)(value & 0xFF);
    return buf_put(b, raw, 4);
}

static void poke_word(unsigned char *at, uint32_t value, int big) {
    if (!big) {
        at[0] = (unsigned char)(value & 0xFF);
        at[1] = (unsigned char)((value >> 8) & 0xFF);
        at[2] = (unsigned char)((value >> 16) & 0xFF);
        at[3] = (unsigned char)((value >> 24) & 0xFF);
        return;
    }
    at[0] = (unsigned char)((value >> 24) & 0xFF);
    at[1] = (unsigned char)((value >> 16) & 0xFF);
    at[2] = (unsigned char)((value >> 8) & 0xFF);
    at[3] = (unsigned char)(value & 0xFF);
}

static void poke_u32(unsigned char *at, uint32_t value) {
    at[0] = (unsigned char)(value & 0xFF);
    at[1] = (unsigned char)((value >> 8) & 0xFF);
    at[2] = (unsigned char)((value >> 16) & 0xFF);
    at[3] = (unsigned char)((value >> 24) & 0xFF);
}

static int buf_grow_to(buf *b, size_t len) {
    if (b->len >= len) {
        return 1;
    }
    return buf_zeros(b, len - b->len);
}

static int chunks_match(const chunk_list *list, const unsigned char *blob,
                        const size_t *offs, const size_t *sizes, size_t count) {
    if (list->count != count) {
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        if (list->items[i].len != sizes[i]) {
            return 0;
        }
        if (sizes[i] > 0 && memcmp(list->items[i].data, blob + offs[i], sizes[i]) != 0) {
            return 0;
        }
    }
    return 1;
}

static void compute_gaps(const size_t *offs, const size_t *sizes, size_t count, size_t *gaps) {
    int have_previous = 0;
    size_t previous_end = 0;
    for (size_t i = 0; i < count; i++) {
        gaps[i] = 0;
        if (sizes[i] == 0) {
            continue;
        }
        if (!have_previous) {
            gaps[i] = offs[i];
            have_previous = 1;
        } else {
            gaps[i] = offs[i] > previous_end ? offs[i] - previous_end : 0;
        }
        previous_end = offs[i] + sizes[i];
    }
}

static int build_simple_block(const chunk_ref *chunks, size_t count, buf *out, int big) {
    buf_reset(out);
    if (!buf_word(out, (uint32_t)count, big)) {
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        if (!buf_word(out, (uint32_t)chunks[i].len, big)) {
            return 0;
        }
        if (chunks[i].len > 0 && !buf_put(out, chunks[i].data, chunks[i].len)) {
            return 0;
        }
    }
    return 1;
}

static int build_relpairtable_block(const unsigned char *blob, const sub_block *block,
                                    const chunk_ref *chunks, size_t count, buf *out, err *e) {
    if (count != block->count) {
        err_set(e, "relative pair-table block expected %zu chunks, folder gave %zu",
                block->count, count);
        return 0;
    }

    size_t *gaps = (size_t *)calloc(count + 1, sizeof(size_t));
    size_t *new_off = (size_t *)calloc(count + 1, sizeof(size_t));
    size_t *new_size = (size_t *)calloc(count + 1, sizeof(size_t));
    if (gaps == NULL || new_off == NULL || new_size == NULL) {
        free(gaps);
        free(new_off);
        free(new_size);
        err_set(e, "out of memory rebuilding a relative pair-table block");
        return 0;
    }
    compute_gaps(block->rels, block->sizes, count, gaps);

    int have_previous = 0;
    size_t previous_end = 0;
    for (size_t i = 0; i < count; i++) {
        if (chunks[i].len > 0) {
            size_t rel = have_previous ? previous_end + gaps[i] : gaps[i];
            new_off[i] = rel;
            new_size[i] = chunks[i].len;
            previous_end = rel + chunks[i].len;
            have_previous = 1;
        } else {
            new_off[i] = 0;
            new_size[i] = 0;
        }
    }

    buf_reset(out);
    size_t raw_len = block->end - block->start;
    if (!buf_put(out, blob + block->start, raw_len)) {
        goto oom;
    }
    size_t minimum_header = 4 + count * 8;
    if (!buf_grow_to(out, minimum_header)) {
        goto oom;
    }

    poke_word((unsigned char *)out->data, (uint32_t)count, block->big_endian);
    for (size_t i = 0; i < count; i++) {
        poke_word((unsigned char *)out->data + 4 + i * 8, (uint32_t)new_off[i],
                  block->big_endian);
        poke_word((unsigned char *)out->data + 4 + i * 8 + 4, (uint32_t)new_size[i],
                  block->big_endian);
    }

    for (size_t i = 0; i < count; i++) {
        if (block->sizes[i] > chunks[i].len) {
            size_t zero_start = block->rels[i] + chunks[i].len;
            size_t zero_end = block->rels[i] + block->sizes[i];
            if (!buf_grow_to(out, zero_end)) {
                goto oom;
            }
            memset((unsigned char *)out->data + zero_start, 0, zero_end - zero_start);
        }
    }

    for (size_t i = 0; i < count; i++) {
        if (chunks[i].len == 0) {
            continue;
        }
        if (!buf_grow_to(out, new_off[i] + chunks[i].len)) {
            goto oom;
        }
        memcpy((unsigned char *)out->data + new_off[i], chunks[i].data, chunks[i].len);
    }

    free(gaps);
    free(new_off);
    free(new_size);
    return 1;

oom:
    free(gaps);
    free(new_off);
    free(new_size);
    err_set(e, "out of memory rebuilding a relative pair-table block");
    return 0;
}

static int build_relpair_block(const unsigned char *blob, const sub_block *block,
                               const chunk_ref *chunks, size_t count, buf *out, err *e) {
    if (count != block->entry_count) {
        err_set(e, "relative pair block expected %zu chunks, folder gave %zu",
                block->entry_count, count);
        return 0;
    }

    size_t payload_base_rel = block->payload_base_rel;
    size_t min_rel = 0;
    int have_min = 0;
    int preserve_layout = 0;

    for (size_t i = 0; i < count; i++) {
        if (block->sizes[i] > 0 && (!have_min || block->rels[i] < min_rel)) {
            min_rel = block->rels[i];
            have_min = 1;
        }
    }

    size_t *order = (size_t *)malloc(sizeof(size_t) * (count + 1));
    if (order == NULL) {
        err_set(e, "out of memory rebuilding a relative pair block");
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        order[i] = i;
    }
    for (size_t i = 1; i < count; i++) {
        size_t key = order[i];
        size_t j = i;
        while (j > 0 && block->rels[order[j - 1]] > block->rels[key]) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = key;
    }
    {
        long long last_end = -1;
        for (size_t i = 0; i < count; i++) {
            size_t idx = order[i];
            if (block->sizes[idx] == 0) {
                continue;
            }
            if ((long long)block->rels[idx] < last_end) {
                preserve_layout = 1;
                break;
            }
            last_end = (long long)(block->rels[idx] + block->sizes[idx]);
        }
    }
    free(order);

    size_t *gaps = (size_t *)calloc(count + 1, sizeof(size_t));
    size_t *new_off = (size_t *)calloc(count + 1, sizeof(size_t));
    size_t *new_size = (size_t *)calloc(count + 1, sizeof(size_t));
    if (gaps == NULL || new_off == NULL || new_size == NULL) {
        free(gaps);
        free(new_off);
        free(new_size);
        err_set(e, "out of memory rebuilding a relative pair block");
        return 0;
    }

    if (preserve_layout) {
        for (size_t i = 0; i < count; i++) {
            new_off[i] = chunks[i].len > 0 ? block->rels[i] : 0;
            new_size[i] = chunks[i].len;
        }
    } else {
        compute_gaps(block->rels, block->sizes, count, gaps);
        int have_previous = 0;
        size_t previous_end = 0;
        for (size_t i = 0; i < count; i++) {
            if (chunks[i].len > 0) {
                size_t rel;
                if (!have_previous) {
                    rel = gaps[i] > 0 ? gaps[i] : (have_min ? min_rel : 0);
                    if (block->sizes[i] > 0) {
                        rel = gaps[i];
                    }
                } else {
                    rel = previous_end + gaps[i];
                }
                new_off[i] = rel;
                new_size[i] = chunks[i].len;
                previous_end = rel + chunks[i].len;
                have_previous = 1;
            } else {
                new_off[i] = 0;
                new_size[i] = 0;
            }
        }
    }

    buf_reset(out);
    size_t raw_len = block->end - block->start;
    if (!buf_put(out, blob + block->start, raw_len)) {
        goto oom;
    }
    if (!buf_grow_to(out, payload_base_rel)) {
        goto oom;
    }

    poke_word((unsigned char *)out->data, (uint32_t)block->declared_count,
              block->big_endian);
    poke_word((unsigned char *)out->data + 4, (uint32_t)payload_base_rel,
              block->big_endian);
    for (size_t i = 0; i < count; i++) {
        poke_word((unsigned char *)out->data + 8 + i * 8, (uint32_t)new_off[i],
                  block->big_endian);
        poke_word((unsigned char *)out->data + 8 + i * 8 + 4, (uint32_t)new_size[i],
                  block->big_endian);
    }

    size_t reserved_len = block->payload_base_abs > block->reserved_start
                              ? block->payload_base_abs - block->reserved_start
                              : 0;
    size_t reserved_start_new = 8 + count * 8;
    if (reserved_len > 0) {
        if (!buf_grow_to(out, reserved_start_new + reserved_len)) {
            goto oom;
        }
        memcpy((unsigned char *)out->data + reserved_start_new,
               blob + block->reserved_start, reserved_len);
    }
    if (!buf_grow_to(out, payload_base_rel)) {
        goto oom;
    }

    for (size_t i = 0; i < count; i++) {
        if (block->sizes[i] > chunks[i].len) {
            size_t abs_off = payload_base_rel + block->rels[i];
            size_t zero_start = abs_off + chunks[i].len;
            size_t zero_end = abs_off + block->sizes[i];
            if (!buf_grow_to(out, zero_end)) {
                goto oom;
            }
            memset((unsigned char *)out->data + zero_start, 0, zero_end - zero_start);
        }
    }

    for (size_t i = 0; i < count; i++) {
        if (chunks[i].len == 0) {
            continue;
        }
        size_t abs_off = payload_base_rel + new_off[i];
        if (!buf_grow_to(out, abs_off + chunks[i].len)) {
            goto oom;
        }
        memcpy((unsigned char *)out->data + abs_off, chunks[i].data, chunks[i].len);
    }

    free(gaps);
    free(new_off);
    free(new_size);
    return 1;

oom:
    free(gaps);
    free(new_off);
    free(new_size);
    err_set(e, "out of memory rebuilding a relative pair block");
    return 0;
}

static int finish_tail(buf *out, const unsigned char *original, size_t original_len,
                       size_t original_end) {
    if (original_end == 0 || original_end >= original_len) {
        return 1;
    }
    size_t tail = original_len - original_end;
    unsigned char filler = original[original_end];
    int uniform_tail = filler == 0 || filler == 0xFF;
    for (size_t i = original_end; i < original_len && uniform_tail; i++) {
        uniform_tail = original[i] == filler;
    }
    static const size_t alignments[] = {16, 8, 4, 2};
    for (size_t i = 0; uniform_tail && tail < 16 && i < sizeof(alignments) / sizeof(alignments[0]); i++) {
        size_t step = alignments[i];
        if (codec_align_up(original_end, step) == original_len) {
            size_t pad = codec_align_up(out->len, step) - out->len;
            int ok = 1;
            for (size_t k = 0; k < pad && ok; k++) {
                ok = buf_putc(out, filler);
            }
            return ok;
        }
    }
    return buf_put(out, original + original_end, tail);
}

static int rebuild_from_chunks(const unsigned char *blob, size_t blob_len, const sub_layout *layout,
                               const chunk_list *chunks, arena *a, buf *out, err *e) {
    buf_reset(out);

    if (layout->kind == LAYOUT_MULTIBLOCK) {
        size_t total = layout->primary.count;
        for (size_t i = 0; i < layout->later_count; i++) {
            total += layout->later[i].count;
        }
        if (chunks->count != total) {
            err_set(e, "subcontainer file count mismatch: folder has %zu files, the multi-block layout maps to %zu slots",
                    chunks->count, total);
            return 0;
        }

        if (!buf_put(out, blob, layout->primary_block_off)) {
            err_set(e, "out of memory rebuilding a multi-block subcontainer");
            return 0;
        }

        buf block_bytes;
        buf_init(&block_bytes);
        size_t taken = 0;

        int ok;
        if (layout->primary.kind == BLOCK_RELPAIRTABLE) {
            ok = build_relpairtable_block(blob, &layout->primary, chunks->items,
                                          layout->primary.count, &block_bytes, e);
        } else {
            ok = build_relpair_block(blob, &layout->primary, chunks->items,
                                     layout->primary.count, &block_bytes, e);
        }
        if (!ok) {
            buf_free(&block_bytes);
            return 0;
        }
        taken += layout->primary.count;
        if (!buf_put(out, block_bytes.data, block_bytes.len)) {
            buf_free(&block_bytes);
            err_set(e, "out of memory rebuilding a multi-block subcontainer");
            return 0;
        }

        size_t *later_offsets = (size_t *)arena_alloc(a, sizeof(size_t) * (layout->later_count + 1));
        if (later_offsets == NULL) {
            buf_free(&block_bytes);
            err_set(e, "out of memory rebuilding a multi-block subcontainer");
            return 0;
        }

        for (size_t b = 0; b < layout->later_count; b++) {
            const sub_block *block = &layout->later[b];
            size_t block_start = align_up_to(out->len, 4);
            if (block->start >= block_start) {
                block_start = block->start;
            }
            if (!buf_grow_to(out, block_start)) {
                buf_free(&block_bytes);
                err_set(e, "out of memory rebuilding a multi-block subcontainer");
                return 0;
            }
            later_offsets[b] = block_start;

            const chunk_ref *group = chunks->items + taken;
            if (block->kind == BLOCK_RAW) {
                if (!buf_put(out, blob + block->start, block->end - block->start)) {
                    buf_free(&block_bytes);
                    err_set(e, "out of memory rebuilding a multi-block subcontainer");
                    return 0;
                }
            } else if (block->kind == BLOCK_SIMPLE) {
                ok = build_simple_block(group, block->count, &block_bytes, block->big_endian);
                if (!ok) {
                    buf_free(&block_bytes);
                    err_set(e, "out of memory rebuilding a simple block");
                    return 0;
                }
                if (!buf_put(out, block_bytes.data, block_bytes.len)) {
                    buf_free(&block_bytes);
                    err_set(e, "out of memory rebuilding a multi-block subcontainer");
                    return 0;
                }
            } else if (block->kind == BLOCK_RELPAIRTABLE) {
                if (!build_relpairtable_block(blob, block, group, block->count, &block_bytes, e)) {
                    buf_free(&block_bytes);
                    return 0;
                }
                if (!buf_put(out, block_bytes.data, block_bytes.len)) {
                    buf_free(&block_bytes);
                    err_set(e, "out of memory rebuilding a multi-block subcontainer");
                    return 0;
                }
            } else {
                if (!build_relpair_block(blob, block, group, block->count, &block_bytes, e)) {
                    buf_free(&block_bytes);
                    return 0;
                }
                if (!buf_put(out, block_bytes.data, block_bytes.len)) {
                    buf_free(&block_bytes);
                    err_set(e, "out of memory rebuilding a multi-block subcontainer");
                    return 0;
                }
            }
            taken += block->count;
        }
        buf_free(&block_bytes);

        poke_word((unsigned char *)out->data, (uint32_t)layout->outer_count,
                  layout->big_endian);
        poke_word((unsigned char *)out->data + 4, (uint32_t)layout->primary_block_off,
                  layout->big_endian);

        size_t anchor_original = 0;
        int have_anchor = 0;
        for (size_t i = 0; i < layout->later_offset_count; i++) {
            if (layout->later_block_offsets[i] >= layout->primary.end) {
                anchor_original = layout->later_block_offsets[i];
                have_anchor = 1;
                break;
            }
        }
        if (!have_anchor) {
            anchor_original = layout->primary.end;
        }
        size_t anchor_new = layout->later_count > 0 ? later_offsets[0] : out->len;

        for (size_t i = 0; i < layout->later_offset_count; i++) {
            size_t delta = anchor_original - layout->later_block_offsets[i];
            poke_word((unsigned char *)out->data + 8 + i * 4,
                      (uint32_t)(anchor_new - delta), layout->big_endian);
        }

        if (layout->later_count == 0 && layout->primary.end < blob_len) {
            int trailer_is_zero = 1;
            for (size_t i = layout->primary.end; i < blob_len; i++) {
                if (blob[i]) {
                    trailer_is_zero = 0;
                    break;
                }
            }
            if (trailer_is_zero) {
                if (!buf_zeros(out, blob_len - layout->primary.end)) {
                    err_set(e, "out of memory rebuilding a multi-block subcontainer");
                    return 0;
                }
            }
        }

        if (layout->has_tail_field) {
            size_t last_header_offset = anchor_new;
            for (size_t i = 0; i < layout->later_offset_count; i++) {
                size_t value = layout->big_endian
                    ? (size_t)(((unsigned char)out->data[8 + i * 4] << 24) |
                               ((unsigned char)out->data[8 + i * 4 + 1] << 16) |
                               ((unsigned char)out->data[8 + i * 4 + 2] << 8) |
                               (unsigned char)out->data[8 + i * 4 + 3])
                    : codec_u32((const unsigned char *)out->data, 8 + i * 4);
                if (value > last_header_offset) {
                    last_header_offset = value;
                }
            }
            size_t tail_span = out->len > last_header_offset ? out->len - last_header_offset : 0;
            poke_word((unsigned char *)out->data + layout->tail_field_off,
                  (uint32_t)tail_span, layout->big_endian);
        }
        return 1;
    }

    if (layout->kind == LAYOUT_WRAPPER_PAIRS) {
        if (chunks->count != layout->wp_count) {
            err_set(e, "subcontainer file count mismatch: folder has %zu files, the wrapper-pair layout maps to %zu slots",
                    chunks->count, layout->wp_count);
            return 0;
        }
        size_t chunk_index = 0;
        for (size_t p = 0; p < layout->pair_count; p++) {
            size_t cursor = layout->pair_cursor[p];
            size_t wbh_abs = layout->wp_off[chunk_index];
            size_t wbh_size = layout->wp_size[chunk_index];
            size_t wbd_abs = layout->wp_off[chunk_index + 1];

            size_t gap_before_len = wbh_abs - (cursor + 16);
            size_t gap_between_len = wbd_abs - (wbh_abs + wbh_size);

            const chunk_ref *wbh = &chunks->items[chunk_index];
            const chunk_ref *wbd = &chunks->items[chunk_index + 1];
            chunk_index += 2;

            size_t wbh_off = 16 + gap_before_len;
            size_t wbd_off = wbh_off + wbh->len + gap_between_len;

            if (!buf_word(out, (uint32_t)wbh_off, layout->big_endian) ||
                !buf_word(out, (uint32_t)wbh->len, layout->big_endian) ||
                !buf_word(out, (uint32_t)wbd_off, layout->big_endian) ||
                !buf_word(out, (uint32_t)wbd->len, layout->big_endian) ||
                !buf_put(out, blob + cursor + 16, gap_before_len) ||
                (wbh->len > 0 && !buf_put(out, wbh->data, wbh->len)) ||
                !buf_put(out, blob + wbh_abs + wbh_size, gap_between_len) ||
                (wbd->len > 0 && !buf_put(out, wbd->data, wbd->len))) {
                err_set(e, "out of memory rebuilding a wrapper-pair subcontainer");
                return 0;
            }
        }
        if (layout->wrapper_end < blob_len &&
            !buf_put(out, blob + layout->wrapper_end, blob_len - layout->wrapper_end)) {
            err_set(e, "out of memory rebuilding a wrapper-pair subcontainer");
            return 0;
        }
        return 1;
    }

    if (layout->kind == LAYOUT_OFFSETS) {
        if (chunks->count != layout->unique_count) {
            err_set(e, "subcontainer file count mismatch: folder has %zu files, the TOC maps to %zu unique slots",
                    chunks->count, layout->unique_count);
            return 0;
        }
        size_t prefix_end = layout->unique_count > 0 ? layout->unique_offsets[0] : layout->table_end;
        if (!buf_put(out, blob, prefix_end)) {
            err_set(e, "out of memory rebuilding a TOC subcontainer");
            return 0;
        }

        size_t *new_offsets = (size_t *)arena_alloc(a, sizeof(size_t) * (layout->unique_count + 1));
        if (new_offsets == NULL) {
            err_set(e, "out of memory rebuilding a TOC subcontainer");
            return 0;
        }
        size_t cursor = prefix_end;
        for (size_t i = 0; i < chunks->count; i++) {
            new_offsets[i] = cursor;
            cursor += layout->child_align ? align_up_to(chunks->items[i].len, layout->child_align)
                                          : chunks->items[i].len;
        }

        poke_word((unsigned char *)out->data, (uint32_t)layout->count, layout->big_endian);
        for (size_t i = 0; i < layout->count; i++) {
            size_t old_offset = layout->offs[i];
            size_t mapped = old_offset;
            for (size_t k = 0; k < layout->unique_count; k++) {
                if (layout->unique_offsets[k] == old_offset) {
                    mapped = new_offsets[k];
                    break;
                }
            }
            poke_word((unsigned char *)out->data + 4 + i * 4, (uint32_t)mapped,
                      layout->big_endian);
        }

        for (size_t i = 0; i < chunks->count; i++) {
            size_t padded = layout->child_align
                                ? align_up_to(chunks->items[i].len, layout->child_align)
                                : chunks->items[i].len;
            if ((chunks->items[i].len > 0 &&
                 !buf_put(out, chunks->items[i].data, chunks->items[i].len)) ||
                !buf_zeros(out, padded - chunks->items[i].len)) {
                err_set(e, "out of memory rebuilding a TOC subcontainer");
                return 0;
            }
        }
        return 1;
    }

    if (layout->kind == LAYOUT_SEQUENTIAL) {
        if (chunks->count != layout->seq_count) {
            err_set(e, "subcontainer file count mismatch: folder has %zu files, the sequential TOC maps to %zu slots",
                    chunks->count, layout->seq_count);
            return 0;
        }
        size_t pad_len = layout->data_start > layout->table_end
                             ? layout->data_start - layout->table_end
                             : 0;
        if (!buf_word(out, (uint32_t)layout->count, layout->big_endian)) {
            err_set(e, "out of memory rebuilding a sequential subcontainer");
            return 0;
        }
        for (size_t i = 0; i < chunks->count; i++) {
            size_t padded = layout->child_align
                                ? align_up_to(chunks->items[i].len, layout->child_align)
                                : chunks->items[i].len;
            if (!buf_word(out, (uint32_t)padded, layout->big_endian)) {
                err_set(e, "out of memory rebuilding a sequential subcontainer");
                return 0;
            }
        }
        if (pad_len > 0 && !buf_zeros(out, pad_len)) {
            err_set(e, "out of memory rebuilding a sequential subcontainer");
            return 0;
        }
        for (size_t i = 0; i < chunks->count; i++) {
            size_t padded = layout->child_align
                                ? align_up_to(chunks->items[i].len, layout->child_align)
                                : chunks->items[i].len;
            if ((chunks->items[i].len > 0 &&
                 !buf_put(out, chunks->items[i].data, chunks->items[i].len)) ||
                !buf_zeros(out, padded - chunks->items[i].len)) {
                err_set(e, "out of memory rebuilding a sequential subcontainer");
                return 0;
            }
        }
        size_t original_end = layout->data_start;
        for (size_t i = 0; i < layout->seq_count; i++) {
            original_end += layout->seq_sizes[i];
        }
        if (!finish_tail(out, blob, blob_len, original_end)) {
            err_set(e, "out of memory rebuilding a sequential subcontainer");
            return 0;
        }
        return 1;
    }

    if (chunks->count != layout->positive_count && chunks->count != layout->count) {
        err_set(e, "subcontainer file count mismatch: folder has %zu files, the pair-table TOC maps to %zu populated or %zu total slots",
                chunks->count, layout->positive_count, layout->count);
        return 0;
    }

    int use_all_slots = chunks->count == layout->count;
    size_t slots = layout->count;

    size_t *gaps = (size_t *)calloc(slots + 1, sizeof(size_t));
    size_t *new_off = (size_t *)calloc(slots + 1, sizeof(size_t));
    size_t *new_size = (size_t *)calloc(slots + 1, sizeof(size_t));
    const unsigned char **payload = (const unsigned char **)calloc(slots + 1, sizeof(unsigned char *));
    if (gaps == NULL || new_off == NULL || new_size == NULL || payload == NULL) {
        free(gaps);
        free(new_off);
        free(new_size);
        free(payload);
        err_set(e, "out of memory rebuilding a pair-table subcontainer");
        return 0;
    }

    for (size_t i = 0; i < slots; i++) {
        payload[i] = NULL;
        new_size[i] = 0;
    }
    if (use_all_slots) {
        for (size_t i = 0; i < slots; i++) {
            payload[i] = chunks->items[i].data;
            new_size[i] = chunks->items[i].len;
        }
    } else {
        for (size_t i = 0; i < layout->positive_count; i++) {
            size_t slot = layout->positive[i];
            payload[slot] = chunks->items[i].data;
            new_size[slot] = chunks->items[i].len;
        }
    }

    compute_gaps(layout->offs, layout->sizes, slots, gaps);
    size_t header_size = 4 + slots * 8;
    int have_previous = 0;
    size_t previous_end = 0;
    for (size_t i = 0; i < slots; i++) {
        if (new_size[i] > 0) {
            size_t off;
            if (!have_previous) {
                off = gaps[i] > 0 ? gaps[i] : header_size;
            } else {
                off = previous_end + gaps[i];
            }
            new_off[i] = off;
            previous_end = off + new_size[i];
            have_previous = 1;
        } else if (layout->offs[i] != 0) {
            new_off[i] = have_previous ? previous_end : header_size;
        } else {
            new_off[i] = 0;
        }
    }

    if (!buf_word(out, (uint32_t)layout->count, layout->big_endian)) {
        goto pt_oom;
    }
    for (size_t i = 0; i < slots; i++) {
        if (!buf_word(out, (uint32_t)new_off[i], layout->big_endian) ||
            !buf_word(out, (uint32_t)new_size[i], layout->big_endian)) {
            goto pt_oom;
        }
    }

    {
        size_t first_positive = 0;
        for (size_t i = 0; i < slots; i++) {
            if (new_size[i] > 0) {
                first_positive = new_off[i];
                break;
            }
        }
        if (first_positive > out->len && !buf_zeros(out, first_positive - out->len)) {
            goto pt_oom;
        }
    }

    for (size_t i = 0; i < slots; i++) {
        if (new_size[i] == 0) {
            continue;
        }
        if (out->len < new_off[i] && !buf_zeros(out, new_off[i] - out->len)) {
            goto pt_oom;
        }
        if (!buf_put(out, payload[i], new_size[i])) {
            goto pt_oom;
        }
    }

    {
        size_t original_end = 0;
        for (size_t i = 0; i < slots; i++) {
            if (layout->sizes[i] > 0 && layout->offs[i] + layout->sizes[i] > original_end) {
                original_end = layout->offs[i] + layout->sizes[i];
            }
        }
        if (!finish_tail(out, blob, blob_len, original_end)) {
            goto pt_oom;
        }
    }

    free(gaps);
    free(new_off);
    free(new_size);
    free(payload);
    return 1;

pt_oom:
    free(gaps);
    free(new_off);
    free(new_size);
    free(payload);
    err_set(e, "out of memory rebuilding a pair-table subcontainer");
    return 0;
}

static size_t kvs_size_at(const unsigned char *data, size_t off) {
    if (codec_big_endian()) {
        return ((size_t)data[off] << 24) | ((size_t)data[off + 1] << 16) |
               ((size_t)data[off + 2] << 8) | (size_t)data[off + 3];
    }
    return codec_u32(data, off);
}

static size_t kvs_stream_end(const unsigned char *blob, size_t n) {
    size_t pos = 0;
    size_t last_end = 0;
    while (pos + 32 <= n) {
        if (memcmp(blob + pos, "KOVS", 4) != 0) {
            size_t scan = pos;
            while (scan + 4 <= n && memcmp(blob + scan, "KOVS", 4) != 0) {
                scan += 4;
            }
            if (scan + 32 > n) {
                break;
            }
            pos = scan;
        }
        size_t size = kvs_size_at(blob, pos + 4);
        if (size == 0 || pos + 32 + size > n) {
            break;
        }
        last_end = pos + 32 + size;
        pos = codec_align_up(last_end, 16);
    }
    return last_end;
}

static size_t kvs_alignment(const unsigned char *blob, size_t n) {
    size_t alignment = 0x800;
    int later_streams = 0;
    size_t pos = 0;
    while (pos + 32 <= n) {
        if (memcmp(blob + pos, "KOVS", 4) != 0) {
            size_t scan = pos;
            while (scan + 4 <= n && memcmp(blob + scan, "KOVS", 4) != 0) {
                scan += 4;
            }
            if (scan + 32 > n) {
                break;
            }
            pos = scan;
        }
        size_t size = kvs_size_at(blob, pos + 4);
        if (size == 0 || pos + 32 + size > n) {
            break;
        }
        if (pos > 0) {
            later_streams++;
            while (alignment > 16 && pos % alignment != 0) {
                alignment /= 2;
            }
        }
        pos = codec_align_up(pos + 32 + size, 16);
    }
    return later_streams > 0 ? alignment : 16;
}

static int finish_kvs_tail(buf *out, const unsigned char *original, size_t original_len,
                           size_t alignment) {
    size_t stream_end = kvs_stream_end(original, original_len);
    if (stream_end > 0 && stream_end < original_len && alignment > 16 &&
        codec_align_up(stream_end, alignment) == original_len) {
        int zero_tail = 1;
        for (size_t i = stream_end; i < original_len && zero_tail; i++) {
            zero_tail = original[i] == 0;
        }
        if (zero_tail) {
            return buf_zeros(out, codec_align_up(out->len, alignment) - out->len);
        }
    }
    return finish_tail(out, original, original_len, stream_end);
}

static int rebuild_kvs_bank_folder(const char *folder, const unsigned char *original,
                                   size_t original_len, buf *out, err *e) {
    folder_file *files = NULL;
    size_t count = 0;
    if (!list_folder_files(folder, &files, &count, e)) {
        return 0;
    }
    static const char *kvs_ext[] = {".kvs"};
    unsigned char fill = original[8];
    buf_reset(out);
    int ok = buf_put(out, original, NESTED_KVS_BANK_HEADER);
    size_t streams = 0;
    for (size_t i = 0; i < count && ok; i++) {
        if (!has_extension(files[i].path, kvs_ext, 1)) {
            continue;
        }
        buf chunk;
        buf_init(&chunk);
        if (!file_read_all(files[i].path, &chunk)) {
            err_set(e, "couldnt read %s", files[i].path);
            buf_free(&chunk);
            ok = 0;
            break;
        }
        if (chunk.len < 32 || memcmp(chunk.data, "KOVS", 4) != 0) {
            err_set(e, "invalid KVS stream in a voice bank rebuild: %s", files[i].path);
            buf_free(&chunk);
            ok = 0;
            break;
        }
        ok = buf_put(out, chunk.data, chunk.len);
        while (ok && out->len % NESTED_KVS_BANK_HEADER != 0) {
            ok = buf_putc(out, fill);
        }
        buf_free(&chunk);
        streams++;
    }
    folder_files_free(files, count);
    if (ok && streams == 0) {
        err_set(e, "the voice bank folder has no .kvs files to rebuild");
        return 0;
    }
    if (!ok) {
        if (!e->set) {
            err_set(e, "out of memory rebuilding a voice bank");
        }
        return 0;
    }
    poke_word((unsigned char *)out->data, (uint32_t)streams, codec_big_endian());
    return 1;
}

static int rebuild_kvs_folder(const char *folder, const unsigned char *original, size_t original_len,
                              buf *out, err *e) {
    folder_file *files = NULL;
    size_t count = 0;
    if (!list_folder_files(folder, &files, &count, e)) {
        return 0;
    }
    size_t alignment = kvs_alignment(original, original_len);

    static const char *kvs_ext[] = {".kvs"};
    buf_reset(out);
    int wrote_any = 0;
    int ok = 1;

    for (size_t i = 0; i < count && ok; i++) {
        if (!has_extension(files[i].path, kvs_ext, 1)) {
            continue;
        }
        buf chunk;
        buf_init(&chunk);
        if (!file_read_all(files[i].path, &chunk)) {
            err_set(e, "couldnt read %s", files[i].path);
            buf_free(&chunk);
            ok = 0;
            break;
        }
        if (chunk.len < 32 || memcmp(chunk.data, "KOVS", 4) != 0) {
            err_set(e, "invalid KVS chunk in folder rebuild: %s", files[i].path);
            buf_free(&chunk);
            ok = 0;
            break;
        }
        size_t size = kvs_size_at((const unsigned char *)chunk.data, 4);
        size_t data_end = 32 + size;
        if (data_end > chunk.len) {
            data_end = chunk.len;
        }
        size_t pad = codec_align_up(out->len, alignment) - out->len;
        if ((pad > 0 && !buf_zeros(out, pad)) || !buf_put(out, chunk.data, data_end)) {
            err_set(e, "out of memory rebuilding KVS");
            buf_free(&chunk);
            ok = 0;
            break;
        }
        buf_free(&chunk);
        wrote_any = 1;
    }

    folder_files_free(files, count);
    if (ok && !wrote_any) {
        err_set(e, "the KVS folder has no .kvs files to rebuild");
        return 0;
    }
    if (ok && !finish_kvs_tail(out, original, original_len, alignment)) {
        err_set(e, "out of memory rebuilding KVS");
        return 0;
    }
    return ok;
}

static int rebuild_mdlk_folder(const char *folder, const unsigned char *original, size_t original_len,
                               buf *out, err *e) {
    arena a;
    arena_init(&a);
    mdlk_layout layout;
    if (!nested_read_mdlk_layout(original, original_len, &a, &layout)) {
        arena_free(&a);
        err_set(e, "the original file isnt a recognized MDLK container");
        return 0;
    }

    folder_file *files = NULL;
    size_t count = 0;
    if (!list_folder_files(folder, &files, &count, e)) {
        arena_free(&a);
        return 0;
    }

    static const char *mdlk_ext[] = {".g1m", ".g1c"};
    size_t matched = 0;
    for (size_t i = 0; i < count; i++) {
        if (has_extension(files[i].path, mdlk_ext, 2)) {
            matched++;
        }
    }
    if (matched != layout.count) {
        err_set(e, "MDLK file count mismatch: folder has %zu payload files, the original maps to %zu slots",
                matched, layout.count);
        folder_files_free(files, count);
        arena_free(&a);
        return 0;
    }

    buf_reset(out);
    int mdlk_big = original_len >= 4 && memcmp(original, "KLDM", 4) == 0;
    int ok = buf_put(out, original, 8) &&
             buf_put(out, mdlk_big
                         ? (const unsigned char[]){(unsigned char)((matched >> 8) & 0xFF),
                                                   (unsigned char)(matched & 0xFF)}
                         : (const unsigned char[]){(unsigned char)(matched & 0xFF),
                                                   (unsigned char)((matched >> 8) & 0xFF)}, 2) &&
             buf_put(out, original + 10, 2) &&
             buf_put(out, original + 12, 4);
    if (!ok) {
        err_set(e, "out of memory rebuilding MDLK");
    }

    for (size_t i = 0; i < count && ok; i++) {
        if (!has_extension(files[i].path, mdlk_ext, 2)) {
            continue;
        }
        buf chunk;
        buf_init(&chunk);
        if (!file_read_all(files[i].path, &chunk) || chunk.len == 0) {
            err_set(e, "couldnt read %s", files[i].path);
            buf_free(&chunk);
            ok = 0;
            break;
        }
        unsigned char *raw = (unsigned char *)chunk.data;
        int child_big = chunk.len >= 4 && (memcmp(raw, "G1M_", 4) == 0 ||
                                           memcmp(raw, "G1CO", 4) == 0);
        int is_g1m = chunk.len >= 4 && (memcmp(raw, "_M1G", 4) == 0 ||
                                        memcmp(raw, "G1M_", 4) == 0);
        int is_g1c = chunk.len >= 4 && (memcmp(raw, "OC1G", 4) == 0 ||
                                        memcmp(raw, "G1CO", 4) == 0);
        if (is_g1m) {
            if (chunk.len < 12) {
                err_set(e, "%s is too small for G1M", files[i].path);
                ok = 0;
            } else {
                poke_word(raw + 8, (uint32_t)chunk.len, child_big);
            }
        } else if (is_g1c) {
            if (chunk.len < 0x10) {
                err_set(e, "%s is too small for G1C", files[i].path);
                ok = 0;
            } else {
                poke_word(raw + 0x0C, (uint32_t)chunk.len, child_big);
            }
        } else {
            err_set(e, "%s isnt a supported MDLK payload (expected _M1G or OC1G)", files[i].path);
            ok = 0;
        }
        if (ok && !buf_put(out, chunk.data, chunk.len)) {
            err_set(e, "out of memory rebuilding MDLK");
            ok = 0;
        }
        buf_free(&chunk);
    }

    if (ok && layout.payload_end < original_len) {
        if (!buf_put(out, original + layout.payload_end, original_len - layout.payload_end)) {
            err_set(e, "out of memory rebuilding MDLK");
            ok = 0;
        }
    }

    folder_files_free(files, count);
    arena_free(&a);
    return ok;
}

static void patch_all_u32(unsigned char *data, size_t start, size_t end,
                          uint32_t old_value, uint32_t new_value) {
    if (old_value == new_value || end < 4 || start + 4 > end) {
        return;
    }
    unsigned char needle[4];
    poke_u32(needle, old_value);
    size_t pos = start;
    while (pos + 4 <= end) {
        if (memcmp(data + pos, needle, 4) == 0) {
            poke_u32(data + pos, new_value);
            pos += 4;
            continue;
        }
        pos++;
    }
}

static int rebuild_kshl_folder(const char *folder, const unsigned char *original, size_t original_len,
                               buf *out, err *e) {
    arena a;
    arena_init(&a);
    kshl_layout layout;
    if (!nested_read_kshl_layout(original, original_len, &a, &layout)) {
        arena_free(&a);
        err_set(e, "the original file isnt a recognized KSHL container");
        return 0;
    }

    folder_file *files = NULL;
    size_t count = 0;
    if (!list_folder_files(folder, &files, &count, e)) {
        arena_free(&a);
        return 0;
    }

    static const char *shader_ext[] = {".vsh", ".psh", ".dxbc", ".bin"};
    chunk_list chunks;
    chunk_list_init(&chunks);
    int ok = 1;

    for (size_t i = 0; i < count && ok; i++) {
        if (!has_extension(files[i].path, shader_ext, 4)) {
            continue;
        }
        buf chunk;
        buf_init(&chunk);
        if (!file_read_all(files[i].path, &chunk) || chunk.len == 0) {
            err_set(e, "couldnt read %s", files[i].path);
            buf_free(&chunk);
            ok = 0;
            break;
        }
        static const char *strict_ext[] = {".vsh", ".psh"};
        if (has_extension(files[i].path, strict_ext, 2) &&
            codec_dx9_ext_at((const unsigned char *)chunk.data, chunk.len, 0) == NULL) {
            err_set(e, "%s doesnt look like DX9 shader bytecode", files[i].path);
            buf_free(&chunk);
            ok = 0;
            break;
        }
        if (!chunk_list_take_buf(&chunks, &chunk)) {
            err_set(e, "out of memory rebuilding KSHL");
            buf_free(&chunk);
            ok = 0;
            break;
        }
        buf_free(&chunk);
    }
    folder_files_free(files, count);

    if (ok && chunks.count != layout.count) {
        err_set(e, "KSHL file count mismatch: folder has %zu payload files, the original maps to %zu shader slots",
                chunks.count, layout.count);
        ok = 0;
    }

    if (ok) {
        int identical = 1;
        for (size_t i = 0; i < layout.count; i++) {
            if (chunks.items[i].len != layout.entries[i].size ||
                memcmp(chunks.items[i].data, original + layout.entries[i].offset,
                       layout.entries[i].size) != 0) {
                identical = 0;
                break;
            }
        }
        if (identical) {
            buf_reset(out);
            ok = buf_put(out, original, original_len);
            if (!ok) {
                err_set(e, "out of memory rebuilding KSHL");
            }
            chunk_list_free(&chunks);
            arena_free(&a);
            return ok;
        }
    }

    if (ok) {
        size_t payload_start = layout.payload_start;
        buf header;
        buf_init(&header);
        if (!buf_put(&header, original, payload_start)) {
            err_set(e, "out of memory rebuilding KSHL");
            ok = 0;
        }

        size_t new_payload_size = 0;
        for (size_t i = 0; i < chunks.count; i++) {
            new_payload_size += chunks.items[i].len;
        }
        size_t new_size = payload_start + new_payload_size;

        if (ok) {
            unsigned char *raw = (unsigned char *)header.data;
            int kshl_big = original_len >= 4 && memcmp(original, "KSHL", 4) == 0;
            poke_word(raw + 0x08, (uint32_t)new_size, kshl_big);
            poke_word(raw + 0xB0, (uint32_t)payload_start, kshl_big);
            poke_word(raw + 0xB4, (uint32_t)new_payload_size, kshl_big);

            size_t cursor_rel = 0;
            for (size_t i = 0; i < chunks.count; i++) {
                uint32_t old_rel = (uint32_t)(layout.entries[i].offset - payload_start);
                uint32_t old_size = (uint32_t)layout.entries[i].size;
                patch_all_u32(raw, 0xB8, header.len, old_rel, (uint32_t)cursor_rel);
                patch_all_u32(raw, 0xB8, header.len, old_size, (uint32_t)chunks.items[i].len);
                cursor_rel += chunks.items[i].len;
            }
            patch_all_u32(raw, 0xB8, header.len, (uint32_t)layout.payload_size, (uint32_t)new_payload_size);
            patch_all_u32(raw, 0xB8, header.len, (uint32_t)layout.payload_end, (uint32_t)new_size);
            patch_all_u32(raw, 0xB8, header.len, (uint32_t)layout.size, (uint32_t)new_size);

            buf_reset(out);
            ok = buf_put(out, header.data, header.len);
            for (size_t i = 0; ok && i < chunks.count; i++) {
                ok = buf_put(out, chunks.items[i].data, chunks.items[i].len);
            }
            if (!ok) {
                err_set(e, "out of memory rebuilding KSHL");
            }
        }
        buf_free(&header);
    }

    chunk_list_free(&chunks);
    arena_free(&a);
    return ok;
}

static int rebuild_zp1_folder(const char *folder, const unsigned char *original,
                              size_t original_len, buf *out, err *e) {
    char **paths = NULL;
    size_t found = 0;
    if (!repack_list_sorted(folder, &paths, &found, e)) {
        return 0;
    }
    if (found != 1) {
        err_set(e, "a zp1 holds one payload and the folder has %zu files", found);
        repack_free_sorted(paths, found);
        return 0;
    }

    buf body;
    buf_init(&body);
    if (!file_read_all(paths[0], &body)) {
        err_set(e, "couldnt read %s", paths[0]);
        buf_free(&body);
        repack_free_sorted(paths, found);
        return 0;
    }
    repack_free_sorted(paths, found);

    buf work;
    buf_init(&work);
    err quiet;
    err_clear(&quiet);
    int same = zp1_decompress(original, original_len, &work, &quiet) &&
               work.len == body.len &&
               (body.len == 0 || memcmp(work.data, body.data, body.len) == 0);
    buf_free(&work);

    int ok;
    if (same) {
        buf_reset(out);
        ok = buf_put(out, original, original_len);
        if (!ok) {
            err_set(e, "out of memory restoring a zp1 entry");
        }
    } else {
        ok = zp1_compress((const unsigned char *)body.data, body.len,
                          zp1_chunk_size(original, original_len),
                          zp1_version(original, original_len), out, e);
    }
    buf_free(&body);
    return ok;
}

static int rebuild_cstb_folder(const char *folder, const unsigned char *original,
                               size_t original_len, buf *out, err *e) {
    char **paths = NULL;
    size_t found = 0;
    if (!repack_list_sorted(folder, &paths, &found, e)) {
        return 0;
    }
    if (found != 1) {
        err_set(e, "a CSTB holds one payload and the folder has %zu files", found);
        repack_free_sorted(paths, found);
        return 0;
    }

    buf body;
    buf_init(&body);
    if (!repack_read_chunk(paths[0], &body, e)) {
        buf_free(&body);
        repack_free_sorted(paths, found);
        return 0;
    }
    repack_free_sorted(paths, found);

    buf work;
    buf_init(&work);
    err quiet;
    err_clear(&quiet);
    int same = btsc_decompress(original, original_len, &work, &quiet) &&
               work.len == body.len &&
               (body.len == 0 || memcmp(work.data, body.data, body.len) == 0);
    buf_free(&work);

    int ok;
    if (same) {
        buf_reset(out);
        ok = buf_put(out, original, original_len);
        if (!ok) {
            err_set(e, "out of memory restoring a CSTB entry");
        }
    } else {
        ok = btsc_compress((const unsigned char *)body.data, body.len,
                           btsc_block_size(original, original_len), out, e);
    }
    buf_free(&body);
    return ok;
}

static int rebuild_colk_folder(const char *folder, const unsigned char *original,
                               size_t original_len, buf *out, err *e) {
    if (!nested_looks_like_colk(original, original_len)) {
        err_set(e, "the original file isnt a recognized COLK container");
        return 0;
    }
    int big = nested_colk_endian(original, original_len);
    size_t count = big ? (size_t)(((uint32_t)original[8] << 24) | ((uint32_t)original[9] << 16) |
                                  ((uint32_t)original[10] << 8) | (uint32_t)original[11])
                       : codec_u32(original, 8);
    size_t header_end = big ? (size_t)(((uint32_t)original[12] << 24) | ((uint32_t)original[13] << 16) |
                                       ((uint32_t)original[14] << 8) | (uint32_t)original[15])
                            : codec_u32(original, 12);

    char **paths = NULL;
    size_t found = 0;
    if (!repack_list_sorted(folder, &paths, &found, e)) {
        return 0;
    }
    if (found != count) {
        err_set(e, "COLK slot mismatch: the folder holds %zu files, the original maps to %zu",
                found, count);
        repack_free_sorted(paths, found);
        return 0;
    }

    buf_reset(out);
    int ok = buf_put(out, original, header_end);
    if (!ok) {
        err_set(e, "out of memory rebuilding a COLK container");
    }

    size_t *offsets = (size_t *)malloc(sizeof(size_t) * count);
    if (offsets == NULL) {
        err_set(e, "out of memory rebuilding a COLK container");
        ok = 0;
    }

    for (size_t i = 0; i < count && ok; i++) {
        while (ok && (out->len % 16) != 0) {
            ok = buf_putc(out, 0);
        }
        offsets[i] = out->len;
        buf chunk;
        buf_init(&chunk);
        if (!file_read_all(paths[i], &chunk)) {
            err_set(e, "couldnt read %s", paths[i]);
            buf_free(&chunk);
            ok = 0;
            break;
        }
        if (chunk.len > 0) {
            ok = buf_put(out, chunk.data, chunk.len);
        }
        buf_free(&chunk);
        if (!ok) {
            err_set(e, "out of memory rebuilding a COLK container");
        }
    }

    if (ok) {
        unsigned char *head = (unsigned char *)out->data;
        for (size_t i = 0; i < count; i++) {
            poke_word(head + 12 + i * 4, (uint32_t)offsets[i], big);
        }
    }

    free(offsets);
    repack_free_sorted(paths, found);
    return ok;
}

static int rebuild_split_wrapper_folder(const char *folder, const unsigned char *original,
                                        size_t original_len, buf *out, err *e) {
    arena a;
    arena_init(&a);
    size_t *offs = NULL;
    size_t *sizes = NULL;
    uint32_t count = 0;
    if (!codec_read_pairtable_entries(original, original_len, &a, &offs, &sizes, &count)) {
        arena_free(&a);
        err_set(e, "the original file isnt a split-zlib wrapper container");
        return 0;
    }

    folder_file *files = NULL;
    size_t file_count = 0;
    if (!list_folder_files(folder, &files, &file_count, e)) {
        arena_free(&a);
        return 0;
    }
    if (file_count != count) {
        err_set(e, "wrapper file count mismatch: folder has %zu files, the original has %u members",
                file_count, count);
        folder_files_free(files, file_count);
        arena_free(&a);
        return 0;
    }

    chunk_list chunks;
    chunk_list_init(&chunks);
    int ok = 1;
    for (size_t i = 0; i < file_count && ok; i++) {
        buf chunk;
        buf_init(&chunk);
        if (!repack_read_chunk(files[i].path, &chunk, e)) {
            buf_free(&chunk);
            ok = 0;
            break;
        }
        if (!chunk_list_take_buf(&chunks, &chunk)) {
            err_set(e, "out of memory rebuilding a split-zlib wrapper");
            buf_free(&chunk);
            ok = 0;
        }
        buf_free(&chunk);
    }
    folder_files_free(files, file_count);

    if (ok && chunks_match(&chunks, original, offs, sizes, count)) {
        buf_reset(out);
        ok = buf_put(out, original, original_len);
        if (!ok) {
            err_set(e, "out of memory rebuilding a split-zlib wrapper");
        }
        chunk_list_free(&chunks);
        arena_free(&a);
        return ok;
    }

    if (ok) {
        size_t table_end = 4 + (size_t)count * 8;
        size_t leading_gap = offs[0] - table_end;

        buf_reset(out);
        ok = buf_u32(out, count) && buf_zeros(out, (size_t)count * 8) &&
             buf_put(out, original + table_end, leading_gap);

        size_t *new_offsets = (size_t *)arena_alloc(&a, sizeof(size_t) * (count + 1));
        if (new_offsets == NULL) {
            ok = 0;
        }

        size_t previous_end = offs[0] + sizes[0];
        for (uint32_t i = 0; ok && i < count; i++) {
            new_offsets[i] = out->len;
            if (chunks.items[i].len > 0) {
                ok = buf_put(out, chunks.items[i].data, chunks.items[i].len);
            }
            if (ok && i + 1 < count) {
                size_t gap = offs[i + 1] > previous_end ? offs[i + 1] - previous_end : 0;
                ok = buf_put(out, original + previous_end, gap);
                previous_end = offs[i + 1] + sizes[i + 1];
            }
        }
        if (ok && previous_end < original_len) {
            ok = buf_put(out, original + previous_end, original_len - previous_end);
        }
        if (ok) {
            for (uint32_t i = 0; i < count; i++) {
                poke_u32((unsigned char *)out->data + 4 + i * 8, (uint32_t)new_offsets[i]);
                poke_u32((unsigned char *)out->data + 8 + i * 8, (uint32_t)chunks.items[i].len);
            }
        } else {
            err_set(e, "out of memory rebuilding a split-zlib wrapper");
        }
    }

    chunk_list_free(&chunks);
    arena_free(&a);
    return ok;
}

static int rebuild_stored_split(const unsigned char *original, const buf *payload, buf *out, err *e) {
    uint32_t block = codec_u32(original, 0x00);
    if (payload->len == 0) {
        err_set(e, "a stored split resource cant hold an empty payload");
        return 0;
    }
    size_t count = (payload->len + block - 1) / block;
    if (count > 0xFFFF || payload->len > 0xFFFFFFFFu) {
        err_set(e, "the payload is too large for a stored split resource");
        return 0;
    }

    buf_reset(out);
    int ok = buf_put(out, original, 4) && buf_u32(out, (uint32_t)count) &&
             buf_u32(out, (uint32_t)payload->len);
    for (size_t i = 0; ok && i < count; i++) {
        size_t start = i * block;
        size_t take = payload->len - start < block ? payload->len - start : block;
        ok = buf_u32(out, (uint32_t)take);
    }
    if (ok) {
        ok = buf_zeros(out, codec_align_up(out->len, 0x80) - out->len);
    }
    for (size_t i = 0; ok && i < count; i++) {
        size_t start = i * block;
        size_t take = payload->len - start < block ? payload->len - start : block;
        ok = buf_put(out, (const unsigned char *)payload->data + start, take) &&
             buf_zeros(out, codec_align_up(out->len, 0x80) - out->len);
    }
    if (!ok) {
        err_set(e, "out of memory rebuilding a stored split resource");
    }
    return ok;
}

static int rebuild_classic_split_folder(const char *folder, const unsigned char *original,
                                        size_t original_len, buf *out, err *e) {
    arena a;
    arena_init(&a);
    split_layout layout;
    int stored_only = 0;
    if (!codec_read_split_layout(original, original_len, &a, &layout)) {
        if (!codec_read_stored_layout(original, original_len, &a, &layout)) {
            arena_free(&a);
            err_set(e, "the original file isnt a classic split-zlib resource");
            return 0;
        }
        stored_only = 1;
    }

    folder_file *files = NULL;
    size_t file_count = 0;
    if (!list_folder_files(folder, &files, &file_count, e)) {
        arena_free(&a);
        return 0;
    }
    if (file_count != 1) {
        err_set(e, "classic split-zlib folders must hold 1 payload file, found %zu", file_count);
        folder_files_free(files, file_count);
        arena_free(&a);
        return 0;
    }

    buf payload;
    buf_init(&payload);
    int ok = repack_read_chunk(files[0].path, &payload, e);
    folder_files_free(files, file_count);
    if (!ok) {
        buf_free(&payload);
        arena_free(&a);
        return 0;
    }

    buf original_payload;
    buf_init(&original_payload);
    const char *ignored = NULL;
    err decode_err;
    err_clear(&decode_err);
    if (codec_decompress_classic_split(original, original_len, &a, &original_payload,
                                       &ignored, &decode_err)) {
        if (original_payload.len == payload.len &&
            (payload.len == 0 || memcmp(original_payload.data, payload.data, payload.len) == 0)) {
            buf_reset(out);
            ok = buf_put(out, original, original_len);
            if (!ok) {
                err_set(e, "out of memory rebuilding a classic split-zlib resource");
            }
            buf_free(&original_payload);
            buf_free(&payload);
            arena_free(&a);
            return ok;
        }
    }
    buf_free(&original_payload);

    if (stored_only) {
        ok = rebuild_stored_split(original, &payload, out, e);
        buf_free(&payload);
        arena_free(&a);
        return ok;
    }

    size_t alignment = 0;
    size_t *extra_gaps = (size_t *)arena_alloc(&a, sizeof(size_t) * (layout.chunk_count + 1));
    if (extra_gaps == NULL) {
        buf_free(&payload);
        arena_free(&a);
        err_set(e, "out of memory rebuilding a classic split-zlib resource");
        return 0;
    }
    if (layout.chunk_count >= 2) {
        static const size_t candidates[5] = {0x80, 0x40, 0x20, 0x10, 4};
        for (int c = 0; c < 5 && alignment == 0; c++) {
            int fits = 1;
            for (uint16_t i = 0; i + 1 < layout.chunk_count; i++) {
                size_t current_end = layout.chunks[i].offset +
                                     (layout.chunks[i].compressed ? 4 : 0) +
                                     layout.chunks[i].payload_size;
                size_t base_next = align_up_to(current_end, candidates[c]);
                if (layout.chunks[i + 1].offset < base_next) {
                    fits = 0;
                    break;
                }
                extra_gaps[i] = layout.chunks[i + 1].offset - base_next;
            }
            if (fits) {
                alignment = candidates[c];
            }
        }
    }

    buf_reset(out);
    ok = buf_put(out, original, 2);
    if (ok) {
        unsigned char head[6];
        head[0] = (unsigned char)(layout.file_type & 0xFF);
        head[1] = (unsigned char)((layout.file_type >> 8) & 0xFF);
        head[2] = (unsigned char)(layout.chunk_count & 0xFF);
        head[3] = (unsigned char)((layout.chunk_count >> 8) & 0xFF);
        head[4] = original[6];
        head[5] = original[7];
        ok = buf_put(out, head, 6) && buf_u32(out, (uint32_t)payload.len);
    }

    chunk_list stored;
    chunk_list_init(&stored);
    size_t cursor = 0;

    for (uint16_t i = 0; ok && i < layout.chunk_count; i++) {
        size_t take;
        if (i + 1 == layout.chunk_count) {
            take = payload.len > cursor ? payload.len - cursor : 0;
        } else {
            size_t want = 0;
            if (layout.chunks[i].compressed) {
                buf piece;
                buf_init(&piece);
                err ignore_err;
                err_clear(&ignore_err);
                if (codec_inflate(original + layout.chunks[i].payload_off,
                                  layout.chunks[i].payload_size, &piece, &ignore_err)) {
                    want = piece.len;
                }
                buf_free(&piece);
            } else {
                want = layout.chunks[i].payload_size;
            }
            size_t available = payload.len > cursor ? payload.len - cursor : 0;
            take = want < available ? want : available;
        }

        buf piece_out;
        buf_init(&piece_out);
        if (layout.chunks[i].compressed) {
            err deflate_err;
            err_clear(&deflate_err);
            if (!codec_deflate((const unsigned char *)payload.data + cursor, take, 9,
                               &piece_out, &deflate_err)) {
                err_set(e, "couldnt recompress split-zlib chunk %u", (unsigned)i);
                buf_free(&piece_out);
                ok = 0;
                break;
            }
            ok = buf_u32(out, (uint32_t)(4 + piece_out.len));
        } else {
            ok = buf_put(&piece_out, (const unsigned char *)payload.data + cursor, take) &&
                 buf_u32(out, (uint32_t)piece_out.len);
        }
        cursor += take;
        if (ok && !chunk_list_take_buf(&stored, &piece_out)) {
            ok = 0;
        }
        buf_free(&piece_out);
    }

    if (ok) {
        size_t header_end = 0x0C + (size_t)layout.chunk_count * 4;
        size_t leading_gap = layout.chunks[0].offset > header_end
                                 ? layout.chunks[0].offset - header_end
                                 : 0;
        ok = buf_put(out, original + header_end, leading_gap);
    }

    for (uint16_t i = 0; ok && i < layout.chunk_count; i++) {
        if (layout.chunks[i].compressed) {
            ok = buf_u32(out, (uint32_t)stored.items[i].len);
        }
        if (ok && stored.items[i].len > 0) {
            ok = buf_put(out, stored.items[i].data, stored.items[i].len);
        }
        if (!ok || i + 1 >= layout.chunk_count) {
            continue;
        }
        if (alignment != 0) {
            size_t target = align_up_to(out->len, alignment) + extra_gaps[i];
            if (target > out->len) {
                ok = buf_zeros(out, target - out->len);
            }
        } else {
            size_t previous_end = layout.chunks[i].offset +
                                  (layout.chunks[i].compressed ? 4 : 0) +
                                  layout.chunks[i].payload_size;
            size_t gap = layout.chunks[i + 1].offset > previous_end
                             ? layout.chunks[i + 1].offset - previous_end
                             : 0;
            ok = buf_put(out, original + previous_end, gap);
        }
    }

    if (ok) {
        size_t last = layout.chunk_count - 1;
        size_t trailing_start = layout.chunks[last].offset +
                                (layout.chunks[last].compressed ? 4 : 0) +
                                layout.chunks[last].payload_size;
        if (trailing_start < original_len) {
            ok = buf_put(out, original + trailing_start, original_len - trailing_start);
        }
    }
    if (!ok) {
        err_set(e, "out of memory rebuilding a classic split-zlib resource");
    }

    chunk_list_free(&stored);
    buf_free(&payload);
    arena_free(&a);
    return ok;
}

static int rebuild_embedded_mdlk_folder(const char *folder, const unsigned char *original,
                                        size_t original_len, buf *out, err *e) {
    arena a;
    arena_init(&a);
    size_t *offs = NULL;
    size_t *sizes = NULL;
    size_t count = 0;
    if (!nested_read_embedded_mdlk_entries(original, original_len, &a, &offs, &sizes, &count)) {
        arena_free(&a);
        err_set(e, "the original file has no supported embedded MDLK resources");
        return 0;
    }

    folder_file *files = NULL;
    size_t file_count = 0;
    if (!list_folder_files(folder, &files, &file_count, e)) {
        arena_free(&a);
        return 0;
    }

    static const char *mdlk_ext[] = {".mdlk"};
    size_t matched = 0;
    for (size_t i = 0; i < file_count; i++) {
        if (has_extension(files[i].path, mdlk_ext, 1)) {
            matched++;
        }
    }
    if (matched != count) {
        err_set(e, "embedded MDLK count mismatch: folder has %zu MDLK files, the wrapper maps to %zu",
                matched, count);
        folder_files_free(files, file_count);
        arena_free(&a);
        return 0;
    }

    buf_reset(out);
    size_t cursor = 0;
    size_t slot = 0;
    int ok = 1;

    for (size_t i = 0; i < file_count && ok; i++) {
        if (!has_extension(files[i].path, mdlk_ext, 1)) {
            continue;
        }
        if (!buf_put(out, original + cursor, offs[slot] - cursor)) {
            err_set(e, "out of memory rebuilding embedded MDLK");
            ok = 0;
            break;
        }
        buf chunk;
        buf_init(&chunk);
        if (!repack_read_chunk(files[i].path, &chunk, e)) {
            buf_free(&chunk);
            ok = 0;
            break;
        }
        if (chunk.len < 16 || memcmp(chunk.data, "MDLK", 4) != 0) {
            err_set(e, "%s isnt a recognized MDLK resource", files[i].path);
            buf_free(&chunk);
            ok = 0;
            break;
        }
        if (!buf_put(out, chunk.data, chunk.len)) {
            err_set(e, "out of memory rebuilding embedded MDLK");
            buf_free(&chunk);
            ok = 0;
            break;
        }
        buf_free(&chunk);
        cursor = offs[slot] + sizes[slot];
        slot++;
    }

    if (ok && cursor < original_len) {
        if (!buf_put(out, original + cursor, original_len - cursor)) {
            err_set(e, "out of memory rebuilding embedded MDLK");
            ok = 0;
        }
    }

    folder_files_free(files, file_count);
    arena_free(&a);
    return ok;
}

static int rebuild_universal_folder(const char *folder, const unsigned char *original,
                                    size_t original_len, buf *out, err *e) {
    arena a;
    arena_init(&a);
    sub_layout layout;
    if (!nested_read_universal_layout(original, original_len, &a, &layout)) {
        arena_free(&a);
        err_set(e, "the original file isnt a supported universal subcontainer");
        return 0;
    }

    folder_file *files = NULL;
    size_t file_count = 0;
    if (!list_folder_files(folder, &files, &file_count, e)) {
        arena_free(&a);
        return 0;
    }
    if (file_count == 0) {
        err_set(e, "the subcontainer folder has no files to rebuild");
        folder_files_free(files, file_count);
        arena_free(&a);
        return 0;
    }

    chunk_list chunks;
    chunk_list_init(&chunks);
    int ok = 1;
    for (size_t i = 0; i < file_count && ok; i++) {
        buf chunk;
        buf_init(&chunk);
        if (!repack_read_chunk(files[i].path, &chunk, e)) {
            buf_free(&chunk);
            ok = 0;
            break;
        }
        if (!chunk_list_take_buf(&chunks, &chunk)) {
            err_set(e, "out of memory rebuilding a subcontainer");
            buf_free(&chunk);
            ok = 0;
        }
        buf_free(&chunk);
    }
    folder_files_free(files, file_count);
    if (!ok) {
        chunk_list_free(&chunks);
        arena_free(&a);
        return 0;
    }

    size_t *ranges_off = NULL;
    size_t *ranges_size = NULL;
    size_t range_count = nested_layout_payload_ranges(original, original_len, &layout, &a,
                                                      &ranges_off, &ranges_size);
    if (chunks_match(&chunks, original, ranges_off, ranges_size, range_count)) {
        buf_reset(out);
        ok = buf_put(out, original, original_len);
        if (!ok) {
            err_set(e, "out of memory rebuilding a subcontainer");
        }
        chunk_list_free(&chunks);
        arena_free(&a);
        return ok;
    }

    size_t single_off = 0;
    size_t single_len = 0;
    int is_kvs = 0;
    if (nested_single_nested_payload(original, original_len, &layout, &a,
                                     &single_off, &single_len, &is_kvs) && !is_kvs) {
        arena inner_arena;
        arena_init(&inner_arena);
        sub_layout inner;
        if (nested_read_universal_layout(original + single_off, single_len, &inner_arena, &inner)) {
            size_t outer_counts[2] = {0, 0};
            size_t inner_counts[2] = {0, 0};
            size_t outer_n = nested_layout_expected_counts(&layout, outer_counts);
            size_t inner_n = nested_layout_expected_counts(&inner, inner_counts);

            int matches_inner = 0;
            int matches_outer = 0;
            for (size_t i = 0; i < inner_n; i++) {
                if (chunks.count == inner_counts[i]) {
                    matches_inner = 1;
                }
            }
            for (size_t i = 0; i < outer_n; i++) {
                if (chunks.count == outer_counts[i]) {
                    matches_outer = 1;
                }
            }

            if (matches_inner && matches_outer && chunks.count == 1) {
                sub_layout shape;
                int payload_kept = nested_read_universal_layout(chunks.items[0].data, chunks.items[0].len,
                                                                &inner_arena, &shape) &&
                                   shape.kind == inner.kind && shape.count == inner.count;
                matches_outer = payload_kept;
            }

            if (matches_inner && !matches_outer) {
                buf inner_out;
                buf_init(&inner_out);
                ok = rebuild_from_chunks(original + single_off, single_len, &inner,
                                         &chunks, &inner_arena, &inner_out, e);
                if (ok) {
                    size_t trailer_len = 0;
                    size_t *r_off = NULL;
                    size_t *r_size = NULL;
                    size_t r_count = nested_layout_payload_ranges(original + single_off, single_len,
                                                                  &inner, &inner_arena, &r_off, &r_size);
                    size_t payload_end = 0;
                    for (size_t i = 0; i < r_count; i++) {
                        if (r_off[i] + r_size[i] > payload_end) {
                            payload_end = r_off[i] + r_size[i];
                        }
                    }
                    if (payload_end > 0 && payload_end < single_len) {
                        trailer_len = single_len - payload_end;
                        ok = buf_put(&inner_out, original + single_off + payload_end, trailer_len);
                    }
                }
                if (ok && range_count == 1 && ranges_off[0] == single_off &&
                    ranges_size[0] > single_len && single_off + ranges_size[0] <= original_len) {
                    ok = finish_tail(&inner_out, original + single_off, ranges_size[0], single_len);
                    if (!ok) {
                        err_set(e, "out of memory rebuilding a nested subcontainer");
                    }
                }
                if (ok) {
                    chunk_list wrapped;
                    chunk_list_init(&wrapped);
                    if (!chunk_list_take_buf(&wrapped, &inner_out)) {
                        err_set(e, "out of memory rebuilding a nested subcontainer");
                        ok = 0;
                    } else {
                        ok = rebuild_from_chunks(original, original_len, &layout, &wrapped, &a, out, e);
                    }
                    chunk_list_free(&wrapped);
                }
                buf_free(&inner_out);
                arena_free(&inner_arena);
                chunk_list_free(&chunks);
                arena_free(&a);
                return ok;
            }
        }
        arena_free(&inner_arena);
    }

    ok = rebuild_from_chunks(original, original_len, &layout, &chunks, &a, out, e);
    chunk_list_free(&chunks);
    arena_free(&a);
    return ok;
}

static int rebuild_pd2_folder(const char *folder, const unsigned char *original,
                              size_t original_len, buf *out, err *e) {
    if (!legacy_looks_like_pd2(original, original_len)) {
        err_set(e, "the original file isnt a PD2 container");
        return 0;
    }

    folder_file *files = NULL;
    size_t count = 0;
    if (!list_folder_files(folder, &files, &count, e)) {
        return 0;
    }
    if (count == 0) {
        folder_files_free(files, count);
        err_set(e, "the PD2 folder has no children to rebuild");
        return 0;
    }

    buf *chunks = (buf *)calloc(count, sizeof(buf));
    if (chunks == NULL) {
        folder_files_free(files, count);
        err_set(e, "out of memory rebuilding a PD2");
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        buf_init(&chunks[i]);
    }

    int ok = 1;
    for (size_t i = 0; i < count && ok; i++) {
        if (!repack_read_chunk(files[i].path, &chunks[i], e)) {
            ok = 0;
        }
    }

    if (ok) {
        buf_reset(out);
        size_t table_end = 4 + count * 4;
        size_t header = table_end + ((16 - (table_end % 16)) % 16);
        unsigned char *head = (unsigned char *)calloc(header, 1);
        if (head == NULL) {
            err_set(e, "out of memory building the PD2 header");
            ok = 0;
        } else {
            head[0] = (unsigned char)(count & 0xFF);
            head[1] = (unsigned char)((count >> 8) & 0xFF);
            head[2] = (unsigned char)((count >> 16) & 0xFF);
            head[3] = (unsigned char)((count >> 24) & 0xFF);
            for (size_t i = 0; i < count; i++) {
                size_t padded = chunks[i].len + ((16 - (chunks[i].len % 16)) % 16);
                size_t units = padded >> 4;
                head[4 + i * 4] = (unsigned char)(units & 0xFF);
                head[5 + i * 4] = (unsigned char)((units >> 8) & 0xFF);
                head[6 + i * 4] = (unsigned char)((units >> 16) & 0xFF);
                head[7 + i * 4] = (unsigned char)((units >> 24) & 0xFF);
            }
            if (!buf_put(out, head, header)) {
                err_set(e, "out of memory writing the PD2 header");
                ok = 0;
            }
            free(head);
        }

        for (size_t i = 0; i < count && ok; i++) {
            if (!buf_put(out, chunks[i].data, chunks[i].len)) {
                err_set(e, "out of memory writing a PD2 child");
                ok = 0;
                break;
            }
            size_t pad = (16 - (chunks[i].len % 16)) % 16;
            if (pad > 0 && !buf_zeros(out, pad)) {
                err_set(e, "out of memory padding a PD2 child");
                ok = 0;
            }
        }
    }

    for (size_t i = 0; i < count; i++) {
        buf_free(&chunks[i]);
    }
    free(chunks);
    folder_files_free(files, count);
    return ok;
}

static int folder_came_from_pd2(const char *folder) {
    size_t len = strlen(folder);
    while (len > 0 && (folder[len - 1] == '\\' || folder[len - 1] == '/')) {
        len--;
    }
    char *sibling = (char *)malloc(len + 5);
    if (sibling == NULL) {
        return 0;
    }
    memcpy(sibling, folder, len);
    memcpy(sibling + len, ".pd2", 5);
    int found = path_is_file(sibling);
    if (!found) {
        memcpy(sibling + len, ".PD2", 5);
        found = path_is_file(sibling);
    }
    free(sibling);
    return found;
}

int repack_from_folder(const char *folder, const unsigned char *original, size_t original_len,
                       buf *out, err *e) {
    arena probe;
    arena_init(&probe);

    if (original_len >= 16 && memcmp(original, "MDLK", 4) == 0) {
        arena_free(&probe);
        return rebuild_mdlk_folder(folder, original, original_len, out, e);
    }
    if (nested_looks_like_kshl(original, original_len)) {
        arena_free(&probe);
        return rebuild_kshl_folder(folder, original, original_len, out, e);
    }
    if (nested_looks_like_colk(original, original_len)) {
        arena_free(&probe);
        return rebuild_colk_folder(folder, original, original_len, out, e);
    }
    if (zp1_looks_like(original, original_len)) {
        arena_free(&probe);
        return rebuild_zp1_folder(folder, original, original_len, out, e);
    }
    if (btsc_looks_like(original, original_len)) {
        arena_free(&probe);
        return rebuild_cstb_folder(folder, original, original_len, out, e);
    }
    if (nested_kvs_bank_count(original, original_len) > 0) {
        arena_free(&probe);
        return rebuild_kvs_bank_folder(folder, original, original_len, out, e);
    }
    if (codec_looks_like_pairtable(original, original_len, &probe)) {
        arena_free(&probe);
        return rebuild_split_wrapper_folder(folder, original, original_len, out, e);
    }
    if (codec_looks_like_classic_split(original, original_len, &probe) ||
        codec_looks_like_stored_split(original, original_len, &probe)) {
        arena_free(&probe);
        return rebuild_classic_split_folder(folder, original, original_len, out, e);
    }
    if (original_len >= 4 && memcmp(original, "KOVS", 4) == 0) {
        arena_free(&probe);
        return rebuild_kvs_folder(folder, original, original_len, out, e);
    }
    if (folder_came_from_pd2(folder) && legacy_looks_like_pd2(original, original_len)) {
        arena_free(&probe);
        return rebuild_pd2_folder(folder, original, original_len, out, e);
    }
    {
        sub_layout layout;
        int has_layout = nested_read_universal_layout(original, original_len, &probe, &layout);
        arena_free(&probe);
        if (has_layout) {
            return rebuild_universal_folder(folder, original, original_len, out, e);
        }
    }
    if (nested_looks_like_embedded_mdlk(original, original_len)) {
        return rebuild_embedded_mdlk_folder(folder, original, original_len, out, e);
    }

    folder_file *leftover = NULL;
    size_t leftover_count = 0;
    err listing = {0};
    int mine = 0;
    if (list_folder_files(folder, &leftover, &leftover_count, &listing)) {
        for (size_t i = 0; i < leftover_count && !mine; i++) {
            const char *slash = strrchr(leftover[i].path, '\\');
            const char *base = slash == NULL ? leftover[i].path : slash + 1;
            if (isdigit((unsigned char)base[0]) && isdigit((unsigned char)base[1]) &&
                isdigit((unsigned char)base[2]) && base[3] == '.') {
                mine = 1;
            } else if (strncmp(base, "entry_", 6) == 0) {
                mine = 1;
            }
        }
        folder_files_free(leftover, leftover_count);
    }
    if (mine) {
        err_set(e, "%s holds unpacked members but the original isnt a "
                   "subcontainer this build knows how to rebuild, so the edits "
                   "would be lost", folder);
        return 0;
    }

    buf_reset(out);
    if (!buf_put(out, original, original_len)) {
        err_set(e, "out of memory copying an unchanged resource");
        return 0;
    }
    return 1;
}

int repack_read_chunk(const char *file_path, buf *out, err *e) {
    buf_reset(out);
    if (!file_read_all(file_path, out)) {
        err_set(e, "couldnt read %s", file_path);
        return 0;
    }

    const char *slash = strrchr(file_path, '\\');
    const char *fname = slash == NULL ? file_path : slash + 1;
    const char *dot = strrchr(fname, '.');
    size_t dir_len = slash == NULL ? 0 : (size_t)(slash - file_path);
    size_t stem_len = dot == NULL ? strlen(fname) : (size_t)(dot - fname);

    char *nested_folder = (char *)malloc(dir_len + 1 + stem_len + 1);
    if (nested_folder == NULL) {
        err_set(e, "out of memory resolving a nested folder");
        return 0;
    }
    if (dir_len > 0) {
        memcpy(nested_folder, file_path, dir_len);
        nested_folder[dir_len] = '\\';
        memcpy(nested_folder + dir_len + 1, fname, stem_len);
        nested_folder[dir_len + 1 + stem_len] = 0;
    } else {
        memcpy(nested_folder, fname, stem_len);
        nested_folder[stem_len] = 0;
    }

    if (!path_is_dir(nested_folder)) {
        free(nested_folder);
        return 1;
    }

    buf original;
    buf_init(&original);
    if (!buf_put(&original, out->data, out->len)) {
        free(nested_folder);
        buf_free(&original);
        err_set(e, "out of memory staging a nested rebuild");
        return 0;
    }

    int ok = repack_from_folder(nested_folder, (const unsigned char *)original.data,
                                original.len, out, e);
    free(nested_folder);
    buf_free(&original);
    return ok;
}
