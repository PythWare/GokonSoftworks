#ifndef ZIP_H
#define ZIP_H
#include "util.h"
#define ZIP_SIG_EOCD 0x06054b50u
#define ZIP_SIG_CEN 0x02014b50u
#define ZIP_SIG_LOC 0x04034b50u
#define ZIP_CEN_FIXED 46
#define ZIP_LOC_FIXED 30
#define ZIP_EOCD_FIXED 22
#define ZIP_METHOD_STORE 0
#define ZIP_METHOD_DEFLATE 8
#define ZIP_U32_CEILING 0xFFFFFFFFull

typedef struct {
    char *name;
    uint16_t name_len;
    uint16_t ver_made;
    uint16_t ver_need;
    uint16_t flag;
    uint16_t method;
    uint16_t dostime;
    uint16_t dosdate;
    uint32_t crc;
    uint32_t csize;
    uint32_t usize;
    uint16_t disk;
    uint16_t int_attr;
    uint32_t ext_attr;
    uint32_t lho;
} zip_entry;

typedef struct {
    zip_entry *entries;
    uint32_t count;
    uint32_t cd_offset;
    uint32_t cd_size;
    int64_t file_size;
} zip_dir;

uint16_t read_u16(const unsigned char *at);
uint32_t read_u32(const unsigned char *at);
void write_u16(unsigned char *at, uint16_t value);
void write_u32(unsigned char *at, uint32_t value);

int zip_read_dir(const unsigned char *base, int64_t size, arena *a, zip_dir *out, err *e);
int zip_name_matches(const zip_entry *entry, const char *wanted);
int zip_find_entry(const zip_dir *dir, const char *wanted);
int zip_data_offset(const unsigned char *base, int64_t size, const zip_entry *entry,
                    uint32_t *out, err *e);
void zip_dos_now(uint16_t *dostime, uint16_t *dosdate);
int zip_put_local_header(buf *out, const zip_entry *entry);
int zip_put_central_entry(buf *out, const zip_entry *entry);
int zip_put_eocd(buf *out, uint32_t count, uint32_t cd_size, uint32_t cd_offset);
int zip_build_directory(buf *out, const zip_entry *entries, uint32_t count,
                        uint32_t cd_offset);

#endif
