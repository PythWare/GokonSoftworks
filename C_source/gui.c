#include "gui.h"
#include "gsfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static gs_app app;

static void group_digits(const char *digits, char *out, size_t room) {
    size_t len = strlen(digits);
    size_t at = 0;
    for (size_t i = 0; i < len && at + 1 < room; i++) {
        if (i > 0 && (len - i) % 3 == 0 && at + 2 < room) {
            out[at++] = ',';
        }
        out[at++] = digits[i];
    }
    out[at] = 0;
}

void gui_thousands(int64_t value, char *out, size_t room) {
    char digits[32];
    unsigned long long magnitude = value < 0 ? (unsigned long long)(-(value + 1)) + 1 : (unsigned long long)value;
    snprintf(digits, sizeof(digits), "%llu", magnitude);
    char grouped[48];
    group_digits(digits, grouped, sizeof(grouped));
    snprintf(out, room, "%s%s", value < 0 ? "-" : "", grouped);
}

void gui_thousands_f1(double value, char *out, size_t room) {
    char text[64];
    snprintf(text, sizeof(text), "%.1f", value < 0 ? -value : value);
    char *dot = strchr(text, '.');
    char whole[48];
    size_t whole_len = dot == NULL ? strlen(text) : (size_t)(dot - text);
    memcpy(whole, text, whole_len);
    whole[whole_len] = 0;
    char grouped[64];
    group_digits(whole, grouped, sizeof(grouped));
    snprintf(out, room, "%s%s%s", value < 0 ? "-" : "", grouped, dot == NULL ? "" : dot);
}

int gui_main(void) {
    srand((unsigned)time(NULL) ^ (unsigned)GetCurrentProcessId());
    if (!ui_init()) {
        MessageBoxW(NULL, L"Couldnt start Direct2D, DirectWrite or WIC on this system.", L"GokonSoftworks",
                    MB_OK | MB_ICONERROR);
        return 1;
    }
    memset(&app, 0, sizeof(app));
    settings_load(&app.settings, gs_project_root());
    app.theme = theme_pick(settings_get_str(&app.settings, "last_theme", ""));
    settings_set_str(&app.settings, "last_theme", app.theme->key);
    settings_save(&app.settings);
    if (!hub_open(&app)) {
        char *log = gs_log_path();
        char text[1024];
        snprintf(text, sizeof(text), "Couldnt start the hub.\n\nLog:\n%s", log == NULL ? "" : log);
        free(log);
        gs_log("ERROR", "Couldnt start the hub");
        ui_message(NULL, "GokonSoftworks", text, MB_ICONERROR);
        ui_shutdown();
        return 1;
    }
    int code = ui_run_loop();
    settings_free(&app.settings);
    ui_shutdown();
    return code;
}
