#include "bottleart.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int bottle_base_load(bottle_base *base, int resource_id) {
    memset(base, 0, sizeof(*base));
    const void *data = NULL;
    size_t len = 0;
    char why[128];
    if (!ui_resource_bytes(resource_id, &data, &len)) {
        return 0;
    }
    ui_image *straight = ui_image_decode_rgba(data, len, why, sizeof(why));
    if (straight == NULL) {
        return 0;
    }
    base->rgba = straight->pixels;
    base->width = straight->width;
    base->height = straight->height;
    straight->pixels = NULL;
    ui_image_free(straight);
    return 1;
}

void bottle_base_free(bottle_base *base) {
    free(base->rgba);
    memset(base, 0, sizeof(*base));
}

float bottle_width_for(const bottle_base *base, float height) {
    double width = nearbyint((double)base->width * height / base->height);
    return width < 1 ? 1.0f : (float)width;
}

static void shade(uint32_t colour, double factor, int out[3]) {
    int parts[3] = {(int)((colour >> 16) & 0xFF), (int)((colour >> 8) & 0xFF), (int)(colour & 0xFF)};
    for (int i = 0; i < 3; i++) {
        double value = nearbyint(parts[i] * factor);
        out[i] = value < 0 ? 0 : (value > 255 ? 255 : (int)value);
    }
}

static int floor_div(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        q--;
    }
    return q;
}

static void colorize_lut(const int black[3], const int white[3], const int mid[3], unsigned char lut[3][256]) {
    for (int c = 0; c < 3; c++) {
        int at = 0;
        for (int i = 0; i < 127; i++) {
            lut[c][at++] = (unsigned char)(black[c] + floor_div(i * (mid[c] - black[c]), 127));
        }
        for (int i = 0; i < 128; i++) {
            lut[c][at++] = (unsigned char)(mid[c] + floor_div(i * (white[c] - mid[c]), 128));
        }
        lut[c][at] = (unsigned char)white[c];
    }
}

ui_image *bottle_render(const bottle_base *base, uint32_t colour, int filled, float height, float scale) {
    int w = base->width;
    int h = base->height;
    unsigned char *out = (unsigned char *)malloc((size_t)w * h * 4);
    if (out == NULL) {
        return NULL;
    }
    int black[3], white[3], mid[3];
    if (filled) {
        shade(colour, 0.30, black);
        shade(colour, 1.55, white);
        shade(colour, 1.0, mid);
    } else {
        int b[3] = {0x08, 0x08, 0x0a};
        int wt[3] = {0x4c, 0x4c, 0x55};
        int m[3] = {0x2a, 0x2a, 0x30};
        memcpy(black, b, sizeof(b));
        memcpy(white, wt, sizeof(wt));
        memcpy(mid, m, sizeof(m));
    }
    unsigned char lut[3][256];
    colorize_lut(black, white, mid, lut);
    int shoulder = (int)(h * BOTTLE_SHOULDER);
    int lx0 = (int)(BOTTLE_LABEL_X0 * w);
    int ly0 = (int)(BOTTLE_LABEL_Y0 * h);
    int lx1 = (int)(BOTTLE_LABEL_X1 * w);
    int ly1 = (int)(BOTTLE_LABEL_Y1 * h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const unsigned char *px = base->rgba + ((size_t)y * w + x) * 4;
            unsigned char *dst = out + ((size_t)y * w + x) * 4;
            unsigned luma = (px[0] * 19595u + px[1] * 38470u + px[2] * 7471u + 0x8000u) >> 16;
            int masked = luma <= BOTTLE_GLASS_LUMA_MAX && px[3] >= 128;
            if (y <= shoulder) {
                masked = 0;
            }
            if (x >= lx0 && x <= lx1 && y >= ly0 && y <= ly1) {
                masked = 0;
            }
            if (masked) {
                dst[0] = lut[0][luma];
                dst[1] = lut[1][luma];
                dst[2] = lut[2][luma];
                dst[3] = 255;
            } else {
                memcpy(dst, px, 4);
            }
        }
    }
    ui_image *full = ui_image_from_rgba(out, w, h);
    free(out);
    if (full == NULL) {
        return NULL;
    }
    float width = bottle_width_for(base, height);
    int pw = (int)(width * scale + 0.5f);
    int ph = (int)(height * scale + 0.5f);
    ui_image *scaled = ui_image_scaled(full, pw < 1 ? 1 : pw, ph < 1 ? 1 : ph);
    ui_image_free(full);
    return scaled;
}

uint32_t bottle_colour(const char *text) {
    uint32_t value = 0;
    if (colour_parse(text, &value)) {
        return value;
    }
    return BOTTLE_DEFAULT_COLOUR;
}
