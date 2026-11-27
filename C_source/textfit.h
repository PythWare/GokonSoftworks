#ifndef TEXTFIT_H
#define TEXTFIT_H
#include "ui.h"

typedef struct {
    wchar_t **items;
    int count;
    int cap;
} ui_lines;

void lines_init(ui_lines *lines);
int lines_push(ui_lines *lines, const wchar_t *text, int len);
void lines_free(ui_lines *lines);

int fit_cut(ui_font *font, const wchar_t *text, int len, float max_width);
wchar_t *fit_line(ui_font *font, const wchar_t *text, int len, float max_width);
void wrap_lines(ui_font *font, const wchar_t *text, int len, float max_width, int max_lines, ui_lines *out);
wchar_t *join_lines(const ui_lines *lines);

#endif
