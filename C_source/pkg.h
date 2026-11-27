#ifndef PKG_H
#define PKG_H
#include "json.h"
#include "proto.h"
#include "schema.h"
#include "util.h"
#define GOKON_MAGIC "GOKON\0"
#define GOKON_MAGIC_LEN 6
#define GOKON_FORMAT_VERSION 2
#define GOKON_MAX_IMAGES 5

typedef struct {
    int64_t entries;
    int64_t payload_bytes;
    int64_t encrypted_entries;
    int64_t compressed_entries;
    int64_t images;
    int64_t image_bytes;
    int64_t audio_bytes;
} bottle_stats;

int pkg_bottle(job_ctx *job, const game_schema *s, int game_index, const char *out_path,
               const json_value *meta, const json_value *entries,
               const json_value *images, const char *audio_path,
               bottle_stats *stats, err *e);

#endif
