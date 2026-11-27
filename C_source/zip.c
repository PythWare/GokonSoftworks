#include "zip.h"
#include <string.h>
#include <time.h>

uint16_t read_u16(const unsigned char *at) {
    return (uint16_t)(at[0] | ((uint16_t)at[1] << 8));
}

uint32_t read_u32(const unsigned char *at) {
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8) | ((uint32_t)at[2] << 16) |
           ((uint32_t)at[3] << 24);
}

void write_u16(unsigned char *at, uint16_t value) {
    at[0] = (unsigned char)(value & 0xFFu);
    at[1] = (unsigned char)((value >> 8) & 0xFFu);
}

void write_u32(unsigned char *at, uint32_t value) {
    at[0] = (unsigned char)(value & 0xFFu);
    at[1] = (unsigned char)((value >> 8) & 0xFFu);
    at[2] = (unsigned char)((value >> 16) & 0xFFu);
    at[3] = (unsigned char)((value >> 24) & 0xFFu);
}

static int64_t find_eocd(const unsigned char *base, int64_t size) {
    if (size < ZIP_EOCD_FIXED) {
        return -1;
    }
    int64_t limit = size - ZIP_EOCD_FIXED;
    int64_t floor = limit > 0xFFFF ? limit - 0xFFFF : 0;
    for (int64_t at = limit; at >= floor; at--) {
        if (read_u32(base + at) == ZIP_SIG_EOCD) {
            uint16_t comment_len = read_u16(base + at + 20);
            if (at + ZIP_EOCD_FIXED + (int64_t)comment_len == size) {
                return at;
            }
        }
    }
    return -1;
}

int zip_read_dir(const unsigned char *base, int64_t size, arena *a, zip_dir *out, err *e) {
    memset(out, 0, sizeof(*out));
    out->file_size = size;

    int64_t eocd = find_eocd(base, size);
    if (eocd < 0) {
        err_set(e, "No end of central directory record, not a zip archive");
        return 0;
    }

    uint16_t disk_num = read_u16(base + eocd + 4);
    uint16_t disk_start = read_u16(base + eocd + 6);
    uint16_t here_count = read_u16(base + eocd + 8);
    uint16_t total_count = read_u16(base + eocd + 10);
    uint32_t cd_size = read_u32(base + eocd + 12);
    uint32_t cd_offset = read_u32(base + eocd + 16);

    if (disk_num != 0 || disk_start != 0 || here_count != total_count) {
        err_set(e, "Split archives arent supported");
        return 0;
    }
    if ((int64_t)cd_offset + (int64_t)cd_size > size) {
        err_set(e, "Central directory runs past the end of the file");
        return 0;
    }

    out->cd_offset = cd_offset;
    out->cd_size = cd_size;
    out->count = total_count;

    if (total_count == 0) {
        out->entries = NULL;
        return 1;
    }

    zip_entry *entries = (zip_entry *)arena_alloc(a, (size_t)total_count * sizeof(zip_entry));
    if (entries == NULL) {
        err_set(e, "Out of memory for %u central directory entries", total_count);
        return 0;
    }

    int64_t at = cd_offset;
    int64_t stop = (int64_t)cd_offset + (int64_t)cd_size;
    for (uint32_t i = 0; i < total_count; i++) {
        if (at + ZIP_CEN_FIXED > stop) {
            err_set(e, "Central directory entry %u is truncated", i);
            return 0;
        }
        const unsigned char *cen = base + at;
        if (read_u32(cen) != ZIP_SIG_CEN) {
            err_set(e, "Central directory entry %u has a bad signature", i);
            return 0;
        }
        zip_entry *entry = &entries[i];
        entry->ver_made = read_u16(cen + 4);
        entry->ver_need = read_u16(cen + 6);
        entry->flag = read_u16(cen + 8);
        entry->method = read_u16(cen + 10);
        entry->dostime = read_u16(cen + 12);
        entry->dosdate = read_u16(cen + 14);
        entry->crc = read_u32(cen + 16);
        entry->csize = read_u32(cen + 20);
        entry->usize = read_u32(cen + 24);
        uint16_t name_len = read_u16(cen + 28);
        uint16_t extra_len = read_u16(cen + 30);
        uint16_t comment_len = read_u16(cen + 32);
        entry->disk = read_u16(cen + 34);
        entry->int_attr = read_u16(cen + 36);
        entry->ext_attr = read_u32(cen + 38);
        entry->lho = read_u32(cen + 42);

        int64_t total = (int64_t)ZIP_CEN_FIXED + name_len + extra_len + comment_len;
        if (at + total > stop) {
            err_set(e, "Central directory entry %u runs past the directory", i);
            return 0;
        }
        if ((entry->flag & 0x0001u) != 0) {
            err_set(e, "Entry %u is encrypted, which isntt supported", i);
            return 0;
        }
        entry->name = arena_strdup(a, (const char *)(cen + ZIP_CEN_FIXED), name_len);
        if (entry->name == NULL) {
            err_set(e, "Out of memory for entry %u name", i);
            return 0;
        }
        entry->name_len = name_len;
        at += total;
    }

    out->entries = entries;
    return 1;
}

static char fold_path_char(char ch) {
    if (ch == '\\') {
        return '/';
    }
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch + 32);
    }
    return ch;
}

int zip_name_matches(const zip_entry *entry, const char *wanted) {
    size_t len = strlen(wanted);
    if (len != entry->name_len) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        if (fold_path_char(entry->name[i]) != fold_path_char(wanted[i])) {
            return 0;
        }
    }
    return 1;
}

int zip_find_entry(const zip_dir *dir, const char *wanted) {
    for (uint32_t i = 0; i < dir->count; i++) {
        if (zip_name_matches(&dir->entries[i], wanted)) {
            return (int)i;
        }
    }
    return -1;
}

int zip_data_offset(const unsigned char *base, int64_t size, const zip_entry *entry,
                    uint32_t *out, err *e) {
    if ((int64_t)entry->lho + ZIP_LOC_FIXED > size) {
        err_set(e, "%s has a local header past the end of the file", entry->name);
        return 0;
    }
    const unsigned char *loc = base + entry->lho;
    if (read_u32(loc) != ZIP_SIG_LOC) {
        err_set(e, "%s has a bad local header signature", entry->name);
        return 0;
    }
    uint16_t name_len = read_u16(loc + 26);
    uint16_t extra_len = read_u16(loc + 28);
    int64_t data = (int64_t)entry->lho + ZIP_LOC_FIXED + name_len + extra_len;
    if (data + (int64_t)entry->csize > size) {
        err_set(e, "%s runs past the end of the file", entry->name);
        return 0;
    }
    *out = (uint32_t)data;
    return 1;
}

void zip_dos_now(uint16_t *dostime, uint16_t *dosdate) {
    time_t now = time(NULL);
    struct tm parts;
    localtime_s(&parts, &now);
    int year = parts.tm_year + 1900;
    if (year < 1980) {
        year = 1980;
    }
    *dosdate = (uint16_t)(((year - 1980) << 9) | ((parts.tm_mon + 1) << 5) | parts.tm_mday);
    *dostime = (uint16_t)((parts.tm_hour << 11) | (parts.tm_min << 5) | (parts.tm_sec / 2));
}

int zip_put_local_header(buf *out, const zip_entry *entry) {
    unsigned char head[ZIP_LOC_FIXED];
    write_u32(head, ZIP_SIG_LOC);
    write_u16(head + 4, entry->ver_need);
    write_u16(head + 6, entry->flag);
    write_u16(head + 8, entry->method);
    write_u16(head + 10, entry->dostime);
    write_u16(head + 12, entry->dosdate);
    write_u32(head + 14, entry->crc);
    write_u32(head + 18, entry->csize);
    write_u32(head + 22, entry->usize);
    write_u16(head + 26, entry->name_len);
    write_u16(head + 28, 0);
    if (!buf_put(out, head, ZIP_LOC_FIXED)) {
        return 0;
    }
    size_t at = out->len;
    if (!buf_put(out, entry->name, entry->name_len)) {
        return 0;
    }
    for (size_t i = at; i < out->len; i++) {
        if (out->data[i] == '/') {
            out->data[i] = '\\';
        }
    }
    return 1;
}

int zip_put_central_entry(buf *out, const zip_entry *entry) {
    unsigned char head[ZIP_CEN_FIXED];
    write_u32(head, ZIP_SIG_CEN);
    write_u16(head + 4, entry->ver_made);
    write_u16(head + 6, entry->ver_need);
    write_u16(head + 8, entry->flag);
    write_u16(head + 10, entry->method);
    write_u16(head + 12, entry->dostime);
    write_u16(head + 14, entry->dosdate);
    write_u32(head + 16, entry->crc);
    write_u32(head + 20, entry->csize);
    write_u32(head + 24, entry->usize);
    write_u16(head + 28, entry->name_len);
    write_u16(head + 30, 0);
    write_u16(head + 32, 0);
    write_u16(head + 34, entry->disk);
    write_u16(head + 36, entry->int_attr);
    write_u32(head + 38, entry->ext_attr);
    write_u32(head + 42, entry->lho);
    if (!buf_put(out, head, ZIP_CEN_FIXED)) {
        return 0;
    }
    return buf_put(out, entry->name, entry->name_len);
}

int zip_put_eocd(buf *out, uint32_t count, uint32_t cd_size, uint32_t cd_offset) {
    unsigned char tail[ZIP_EOCD_FIXED];
    write_u32(tail, ZIP_SIG_EOCD);
    write_u16(tail + 4, 0);
    write_u16(tail + 6, 0);
    write_u16(tail + 8, (uint16_t)count);
    write_u16(tail + 10, (uint16_t)count);
    write_u32(tail + 12, cd_size);
    write_u32(tail + 16, cd_offset);
    write_u16(tail + 20, 0);
    return buf_put(out, tail, ZIP_EOCD_FIXED);
}

int zip_build_directory(buf *out, const zip_entry *entries, uint32_t count,
                        uint32_t cd_offset) {
    buf_reset(out);
    for (uint32_t i = 0; i < count; i++) {
        if (!zip_put_central_entry(out, &entries[i])) {
            return 0;
        }
    }
    uint32_t cd_size = (uint32_t)out->len;
    return zip_put_eocd(out, count, cd_size, cd_offset);
}
