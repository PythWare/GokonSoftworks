#ifndef BOTTLEART_H
#define BOTTLEART_H
#include "ui.h"
#define BOTTLE_LABEL_X0 0.085
#define BOTTLE_LABEL_Y0 0.4333
#define BOTTLE_LABEL_X1 0.915
#define BOTTLE_LABEL_Y1 0.7564
#define BOTTLE_SHOULDER 0.3431
#define BOTTLE_GLASS_LUMA_MAX 150
#define BOTTLE_DEFAULT_COLOUR 0x7B1E3A

typedef struct {
    unsigned char *rgba;
    int width;
    int height;
} bottle_base;

int bottle_base_load(bottle_base *base, int resource_id);
void bottle_base_free(bottle_base *base);
float bottle_width_for(const bottle_base *base, float height);
ui_image *bottle_render(const bottle_base *base, uint32_t colour, int filled, float height, float scale);
uint32_t bottle_colour(const char *text);

#endif
