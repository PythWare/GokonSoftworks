#ifndef WIDGETS_H
#define WIDGETS_H
#include "textfit.h"
#include "ui.h"
#define LOG_MAX_LINES 400
#define LOG_BAR_WIDTH 7
#define LOG_THUMB_MIN 20
#define LOG_BLOCK_CHARS 2400
#define LOG_BLOCK_CACHE 64
#define MENU_ROW_HEIGHT 30
#define MENU_DOT_RADIUS 5
#define MENU_DOT_INSET 10
#define MENU_MAX_ROWS 64
#define MENU_MAX_PAGES 8
#define SPARKLE_SIZE 34
#define SPARKLE_FRAMES 14

typedef enum {
    TONE_NORMAL = 0,
    TONE_ACCENT,
    TONE_DANGER
} ui_tone;

typedef enum {
    INK_MUTED = 0,
    INK_TEXT,
    INK_OK,
    INK_DANGER,
    INK_ACCENT
} ui_ink;

typedef void (*ui_action)(void *ctx);

typedef struct {
    float x, y, w, h;
    wchar_t *title;
    uint32_t fill;
    int has_fill;
} ui_panel;

typedef struct {
    float x, y, w, h;
    wchar_t *label;
    ui_tone tone;
    int enabled;
    int hovering;
    int pressed;
    ui_action command;
    void *ctx;
} ui_button;

typedef struct {
    float x, y, w, h;
    float fraction;
} ui_progress;

typedef struct {
    wchar_t *text;
    uint32_t colour;
} log_line;

typedef struct {
    int index;
    ui_lines lines;
} log_wrap;

typedef struct {
    float x, y, w, h;
    ui_font *font;
    float line_height;
    int follow_tail;
    log_line messages[LOG_MAX_LINES + 1];
    int message_count;
    log_line lines[LOG_MAX_LINES + 64];
    int line_count;
    uint32_t block_colour;
    int offset;
    wchar_t **blocks;
    int block_count;
    log_wrap cache[LOG_BLOCK_CACHE + 1];
    int cache_count;
    int top_block;
    int top_line;
    double avg_lines;
    int dragging;
    float drag_grab;
    const gs_theme *theme;
} ui_log;

typedef struct {
    float x, y;
    float width, height;
    float polygon[28];
    float top_y, bottom_y, arc, fill_limit;
    int level;
    ui_image *source;
    ui_image *glass;
    float glass_scale;
    wchar_t *caption;
    int caption_ok;
} ui_gauge;

typedef struct {
    char key[32];
    wchar_t *label;
    wchar_t *note;
    int page;
    float x0, y0, x1, y1;
} menu_row;

typedef struct {
    menu_row rows[MENU_MAX_ROWS];
    int row_count;
    wchar_t *headings[MENU_MAX_PAGES];
    int page_count;
    int page;
    char selected[32];
    int hover;
    float x, y, width;
    int columns;
    int placed;
    void (*on_pick)(void *ctx, const char *key);
    void *ctx;
} ui_menu;

typedef struct {
    float x, y;
    int frame;
} ui_sparkle;

uint32_t ink_colour(const gs_theme *theme, ui_ink ink);

void panel_init(ui_panel *p, float x, float y, float w, float h, const char *title);
void panel_draw(ui_window *win, const ui_panel *p);

void button_init(ui_button *b, float x, float y, float w, float h, const char *label,
                 ui_action command, void *ctx, ui_tone tone);
void button_free(ui_button *b);
void button_set_text(ui_button *b, const char *label);
void button_draw(ui_window *win, const ui_button *b);
int button_hit(const ui_button *b, float x, float y);
int buttons_move(ui_button **list, int count, float x, float y);
void buttons_down(ui_button **list, int count, float x, float y);
void buttons_up(ui_button **list, int count, float x, float y);
void buttons_leave(ui_button **list, int count);

void progress_init(ui_progress *p, float x, float y, float w, float h);
int progress_set(ui_progress *p, float fraction);
void progress_draw(ui_window *win, const gs_theme *theme, const ui_progress *p);

void log_init(ui_log *log, const gs_theme *theme, float x, float y, float w, float h,
              const wchar_t *family, float points, int follow_tail);
void log_free(ui_log *log);
void log_write(ui_log *log, const char *text, ui_ink ink);
void log_set_text(ui_log *log, const char *text, ui_ink ink);
void log_clear(ui_log *log);
void log_place(ui_log *log, float x, float y, float w, float h);
void log_draw(ui_window *win, ui_log *log);
int log_wheel(ui_log *log, float x, float y, int delta);
int log_begin_drag(ui_log *log, float x, float y);
void log_drag_to(ui_log *log, float y);
void log_end_drag(ui_log *log);

int gauge_init(ui_gauge *g, float x, float y, float height, int resource_id);
void gauge_free(ui_gauge *g);
void gauge_rescale(ui_gauge *g, float scale);
int gauge_set(ui_gauge *g, float fraction);
void gauge_caption(ui_gauge *g, const char *text, int ok);
void gauge_place(ui_gauge *g, float x, float y);
void gauge_draw(ui_window *win, ui_gauge *g);

void menu_init(ui_menu *m, void (*on_pick)(void *ctx, const char *key), void *ctx);
void menu_free(ui_menu *m);
void menu_add_page(ui_menu *m, const char *heading);
void menu_add_row(ui_menu *m, const char *key, const char *label, const char *note);
int menu_rows_per_page(const ui_menu *m);
void menu_select(ui_menu *m, const char *key);
void menu_place(ui_menu *m, float x, float y, float width, int columns);
void menu_draw(ui_window *win, ui_menu *m);
int menu_move(ui_menu *m, float x, float y);
int menu_click(ui_menu *m, float x, float y);

void sparkle_draw(ui_window *win, float cx, float cy, uint32_t rgb, int frame);
int sparkle_cluster(ui_sparkle *out, int room, int spread);

#endif
