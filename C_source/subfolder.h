#ifndef SUBFOLDER_H
#define SUBFOLDER_H
#include "util.h"

char *subfolder_for(const char *file_path, int own);
char *subfolder_pseudo_path(const char *file_path);
int subfolder_matches(const char *folder_name, const char *file_name);
int subfolder_mark_shared(char *const *rel, int64_t count, int *own);

#endif
