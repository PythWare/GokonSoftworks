#ifndef REPACK_H
#define REPACK_H
#include "nested.h"
#include "util.h"

int repack_read_chunk(const char *file_path, buf *out, err *e);
int repack_from_folder(const char *folder, const unsigned char *original, size_t original_len,
                       buf *out, err *e);

int repack_list_sorted(const char *folder, char ***paths_out, size_t *count_out, err *e);
void repack_free_sorted(char **paths, size_t count);

#endif
