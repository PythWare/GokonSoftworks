#ifndef EVTSCRIPT_H
#define EVTSCRIPT_H
#include "util.h"

typedef struct {
    uint32_t count;
    size_t *offs;
    size_t *sizes;
} evt_layout;

typedef struct {
    size_t head;
    uint32_t count;
    size_t *offs;
    size_t *sizes;
} evt_section_layout;

int evt_read(const unsigned char *data, size_t len, arena *a, evt_layout *out);
int evt_build(const unsigned char *original, const evt_layout *layout, const unsigned char *const *chunks,
              const size_t *lens, size_t count, buf *out, err *e);
int evt_section_read(const unsigned char *data, size_t len, arena *a, evt_section_layout *out);
int evt_section_build(const unsigned char *original, const evt_section_layout *layout,
                      const unsigned char *const *chunks, const size_t *lens, size_t count, buf *out, err *e);

#endif
