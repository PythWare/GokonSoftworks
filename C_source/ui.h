#ifndef UI_H
#define UI_H
#ifndef COBJMACROS
#define COBJMACROS
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include "proto.h"
#include "settings.h"
#include "util.h"
#define UI_WM_JOB (WM_APP + 1)
#define UI_WM_PROGRESS (WM_APP + 2)
#define UI_MAX_FONTS 32
#define UI_MAX_DIALOGS 8

typedef enum {
    UI_NW = 0, UI_N, UI_NE, UI_W, UI_CENTER, UI_E, UI_SW, UI_S, UI_SE
} ui_anchor;

typedef struct {
    wchar_t family[32];
    float points;
    int bold;
    IDWriteTextFormat *format;
    float line_space;
} ui_font;

typedef struct {
    int width;
    int height;
    unsigned char *pixels;
    ID2D1Bitmap *bitmap;
    void *owner;
} ui_image;

typedef struct ui_window ui_window;
typedef struct ui_job ui_job;

typedef struct {
    void (*paint)(ui_window *w);
    void (*mouse_move)(ui_window *w, float x, float y);
    void (*mouse_down)(ui_window *w, float x, float y);
    void (*mouse_up)(ui_window *w, float x, float y);
    void (*mouse_leave)(ui_window *w);
    void (*wheel)(ui_window *w, float x, float y, int delta);
    void (*timer)(ui_window *w, UINT_PTR id);
    void (*job_progress)(ui_window *w, ui_job *job, int64_t done, int64_t total);
    void (*job_done)(ui_window *w, ui_job *job);
    void (*dpi_changed)(ui_window *w);
    LRESULT (*message)(ui_window *w, UINT msg, WPARAM wp, LPARAM lp, int *handled);
    int (*closing)(ui_window *w);
    void (*destroyed)(ui_window *w);
} ui_handlers;

struct ui_window {
    HWND hwnd;
    ID2D1HwndRenderTarget *target;
    ID2D1SolidColorBrush *brush;
    UINT dpi;
    float scale;
    float width;
    float height;
    const gs_theme *theme;
    const ui_handlers *on;
    int hand_cursor;
    int tracking;
    void *self;
};

typedef int (*ui_job_fn)(ui_job *job);
typedef void (*ui_job_free_fn)(ui_job *job);

struct ui_job {
    job_ctx ctx;
    HWND hwnd;
    int kind;
    int serial;
    int ok;
    err e;
    volatile LONG progress_pending;
    volatile LONGLONG done;
    volatile LONGLONG total;
    void *data;
    ui_job_fn run;
    ui_job_free_fn release;
};

int ui_init(void);
void ui_shutdown(void);
int ui_run_loop(void);
void ui_register_dialog(HWND hwnd);
void ui_unregister_dialog(HWND hwnd);

ui_window *ui_window_create(const wchar_t *title, float width, float height, HWND owner,
                            const gs_theme *theme, const ui_handlers *on, void *self);
void ui_invalidate(ui_window *w);
void ui_set_title(ui_window *w, const wchar_t *title);
void ui_bring_forward(HWND hwnd);
void ui_keep_front(HWND hwnd);
int ui_px(ui_window *w, float dips);

ui_font *ui_font_get(const wchar_t *family, float points, int bold);
float ui_measure(ui_font *font, const wchar_t *text, int len);

void ui_fill_rect(ui_window *w, float x0, float y0, float x1, float y1, uint32_t rgb);
void ui_fill_rect_alpha(ui_window *w, float x0, float y0, float x1, float y1, uint32_t rgb, float alpha);
void ui_stroke_rect(ui_window *w, float x0, float y0, float x1, float y1, uint32_t rgb, float width);
void ui_line(ui_window *w, float x0, float y0, float x1, float y1, uint32_t rgb, float width);
void ui_fill_ellipse(ui_window *w, float x0, float y0, float x1, float y1, uint32_t rgb);
void ui_fill_half_ellipse(ui_window *w, float x0, float y0, float x1, float y1, uint32_t rgb);
void ui_fill_polygon(ui_window *w, const float *points, int count, uint32_t rgb, float alpha);
int ui_push_polygon_clip(ui_window *w, const float *points, int count);
void ui_pop_layer(ui_window *w);
void ui_push_clip(ui_window *w, float x0, float y0, float x1, float y1);
void ui_pop_clip(ui_window *w);
void ui_text(ui_window *w, float x, float y, const wchar_t *text, int len, ui_font *font,
             uint32_t rgb, ui_anchor anchor, float wrap, int centered);
void ui_text_utf8(ui_window *w, float x, float y, const char *text, ui_font *font, uint32_t rgb,
                  ui_anchor anchor, float wrap, int centered);

ui_image *ui_image_decode(const void *data, size_t len, char *why, size_t room);
ui_image *ui_image_decode_rgba(const void *data, size_t len, char *why, size_t room);
ui_image *ui_image_from_rgba(const unsigned char *rgba, int width, int height);
ui_image *ui_image_scaled(const ui_image *src, int width, int height);
ui_image *ui_image_resource(int id);
int ui_resource_bytes(int id, const void **data, size_t *len);
void ui_image_free(ui_image *image);
void ui_draw_image(ui_window *w, ui_image *image, float x0, float y0, float x1, float y1);
int ui_image_decodes(const char *path);

void ui_message(HWND owner, const char *title, const char *text, UINT icon);
int ui_ask_yes_no(HWND owner, const char *title, const char *text);
char *ui_pick_folder(HWND owner, const char *title, const char *initial);
char *ui_pick_file(HWND owner, const char *title, const char *initial_dir, const char *initial_name,
                   const wchar_t *filter_name, const wchar_t *filter_spec);
int ui_pick_files(HWND owner, const char *title, const wchar_t *filter_name,
                  const wchar_t *filter_spec, char ***out, size_t *count);
void ui_open_explorer(const char *path);

ui_job *ui_job_new(HWND hwnd, int kind, int serial, ui_job_fn run, ui_job_free_fn release, void *data);
int ui_job_start(ui_job *job);
void ui_job_report(ui_job *job, int64_t done, int64_t total);
void ui_job_free(ui_job *job);

wchar_t *ui_wide(const char *text);

#endif
