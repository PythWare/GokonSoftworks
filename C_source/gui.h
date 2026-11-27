#ifndef GUI_H
#define GUI_H
#include "gamedesc.h"
#include "settings.h"
#include "ui.h"
#define RES_BOTTLE 101
#define RES_GLASS 102

typedef struct {
    gs_settings settings;
    const gs_theme *theme;
    HWND hub;
    HWND shelf;
    HWND creator;
} gs_app;

int gui_main(void);
void gui_thousands(int64_t value, char *out, size_t room);
void gui_thousands_f1(double value, char *out, size_t room);

int hub_open(gs_app *app);
HWND shelf_open(gs_app *app, HWND owner, const gs_game *game, const char *game_dir, char *why, size_t room);
HWND creator_open(gs_app *app, HWND owner, const gs_game *game, const char *unpack_root, char *why,
                  size_t room);

#endif
