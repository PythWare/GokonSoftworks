#ifndef LINKV2_H
#define LINKV2_H
#include "proto.h"
#include "unpack.h"
#include "util.h"
#define LINKV2_HEADER_SIZE 16
#define LINKV2_ENTRY_SIZE 16
#define LINKV2_MAX_CANDIDATES 8
#define LINKV2_MAX_ROOTS 2
#define LINKV2_NAME_MAX 260
#define LINKV2_MAGIC_AOT 0x00077DF9u
#define LINKV2_MAGIC_OP3 0x00011E54u

typedef enum {
    LINKV2_SIZE_GAP = 0,
    LINKV2_SIZE_TOC
} linkv2_size_rule;

typedef struct {
    const char *pack;
    const char *ref_key;
    const char *candidates[LINKV2_MAX_CANDIDATES];
    int candidate_count;
} linkv2_container;

typedef struct {
    const char *game_id;
    uint32_t magic;
    int shift_bits;
    linkv2_size_rule size_rule;
    const char *roots[LINKV2_MAX_ROOTS];
    int root_count;
    const linkv2_container *containers;
    int container_count;
} linkv2_game;

typedef struct {
    uint32_t magic;
    uint32_t count;
    uint32_t alignment;
} linkv2_header;

const linkv2_game *linkv2_game_for(const char *game_id);
int linkv2_container_count(const char *game_id);
const linkv2_container *linkv2_container_at(const char *game_id, int index);

int linkv2_is_magic(uint32_t magic);
int linkv2_read_header(const char *path, linkv2_header *out);
int linkv2_path_count(const linkv2_game *game, const linkv2_container *c);
int linkv2_path_at(const linkv2_game *game, const linkv2_container *c, int index,
                   char *out, size_t room);
int linkv2_resolve(const char *base_dir, const linkv2_game *game, const linkv2_container *c,
                   char *out, size_t room);

int linkv2_zl_looks_like(const unsigned char *data, size_t len);
int linkv2_zl_decompress(const unsigned char *data, size_t len, buf *out, err *e);

int linkv2_run(job_ctx *job, const unpack_opts *opts, unpack_stats *stats,
               manifest_writer *manifest, err *e);

#endif
