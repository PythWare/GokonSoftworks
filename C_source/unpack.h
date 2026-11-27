#ifndef UNPACK_H
#define UNPACK_H
#include "json.h"
#include "proto.h"
#include "schema.h"
#include "util.h"
#define TAILDATA_FORMAT "gokon-taildata"
#define TAILDATA_VERSION 1

typedef struct {
    const game_schema *schema;
    const char *base_dir;
    const char *out_root;
    const char *state_dir;
    const char *ref_dir;
    int write_files;
} unpack_opts;

typedef struct {
    int64_t entries_seen;
    int64_t files_written;
    int64_t nested_written;
    int64_t decompress_failures;
    int64_t skipped;
} unpack_stats;

typedef struct {
    json_writer w;
    FILE *sink;
    int open;
    int64_t count;
} manifest_writer;

void manifest_container(manifest_writer *m, int idx_marker, const char *container_path);
void manifest_files_open(manifest_writer *m);
void manifest_record(manifest_writer *m, const char *key, int idx_marker,
                     int64_t entry_off, int comp_marker, const char *container,
                     int64_t entry_index, int64_t unpacked_size, const char *ext,
                     const char *name, int rate, int channels,
                     const char *codec, float codec_version,
                     int64_t codec_chunk);

char *taildata_manifest_path(const char *state_dir, const char *game_id);
int unpack_run(job_ctx *job, const unpack_opts *opts, unpack_stats *stats, err *e);

#endif
