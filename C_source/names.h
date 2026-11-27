#ifndef NAMES_H
#define NAMES_H
#include "util.h"

typedef struct {
    char **items;
    int64_t count;
    char *backing;
} name_list;

void name_list_free(name_list *list);
int name_list_load(name_list *list, const char *ref_dir, const char *game_id,
                   const char *pack);
const char *name_at(const name_list *list, int64_t index);

int names_make_unique(char **rel, const int64_t *slot, int64_t count, err *e);

#endif
