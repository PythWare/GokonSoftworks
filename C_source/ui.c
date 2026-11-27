#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include "ui.h"
#include <shellapi.h>
#include <shobjidl.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define UI_CLASS L"GokonSoftworksWindow"
#define UI_LAYER_DEPTH 8

static ID2D1Factory *d2d;
static IDWriteFactory *dwrite;
static IWICImagingFactory *wic;
static ui_font fonts[UI_MAX_FONTS];
static int font_count;
static HWND dialogs[UI_MAX_DIALOGS];
static SRWLOCK backend_lock = SRWLOCK_INIT;
static ID2D1Layer *layer_stack[UI_LAYER_DEPTH];
static ID2D1PathGeometry *geometry_stack[UI_LAYER_DEPTH];
static int layer_depth;

static D2D1_COLOR_F colour_of(uint32_t rgb, float alpha) {
    D2D1_COLOR_F c;
    c.r = (float)((rgb >> 16) & 0xFF) / 255.0f;
    c.g = (float)((rgb >> 8) & 0xFF) / 255.0f;
    c.b = (float)(rgb & 0xFF) / 255.0f;
    c.a = alpha;
    return c;
}

wchar_t *ui_wide(const char *text) {
    wchar_t *wide = utf8_to_wide(text == NULL ? "" : text);
    if (wide == NULL) {
        wide = (wchar_t *)calloc(1, sizeof(wchar_t));
    }
    return wide;
}

static void gui_hook(job_ctx *ctx, const char *kind, int64_t done, int64_t total, const char *text) {
    (void)text;
    ui_job *job = (ui_job *)ctx;
    if (job == NULL || ctx->id != (int64_t)(intptr_t)job) {
        return;
    }
    if (strcmp(kind, "progress") == 0) {
        ui_job_report(job, done, total);
    }
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

int ui_init(void) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) {
        return 0;
    }
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **)&d2d))) {
        return 0;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory, (IUnknown **)&dwrite))) {
        return 0;
    }
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IWICImagingFactory, (void **)&wic))) {
        return 0;
    }
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    wc.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
    wc.lpszClassName = UI_CLASS;
    if (!RegisterClassExW(&wc)) {
        return 0;
    }
    proto_init();
    proto_set_hook(gui_hook);
    return 1;
}

void ui_shutdown(void) {
    for (int i = 0; i < font_count; i++) {
        if (fonts[i].format != NULL) {
            IDWriteTextFormat_Release(fonts[i].format);
        }
    }
    font_count = 0;
    if (wic != NULL) {
        IWICImagingFactory_Release(wic);
    }
    if (dwrite != NULL) {
        IDWriteFactory_Release(dwrite);
    }
    if (d2d != NULL) {
        ID2D1Factory_Release(d2d);
    }
    proto_set_hook(NULL);
    CoUninitialize();
}

void ui_register_dialog(HWND hwnd) {
    for (int i = 0; i < UI_MAX_DIALOGS; i++) {
        if (dialogs[i] == NULL) {
            dialogs[i] = hwnd;
            return;
        }
    }
}

void ui_unregister_dialog(HWND hwnd) {
    for (int i = 0; i < UI_MAX_DIALOGS; i++) {
        if (dialogs[i] == hwnd) {
            dialogs[i] = NULL;
        }
    }
}

int ui_run_loop(void) {
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        HWND root = msg.hwnd == NULL ? NULL : GetAncestor(msg.hwnd, GA_ROOT);
        int eaten = 0;
        for (int i = 0; i < UI_MAX_DIALOGS && !eaten; i++) {
            if (dialogs[i] != NULL && dialogs[i] == root && msg.hwnd != root &&
                (msg.message == WM_KEYDOWN || msg.message == WM_CHAR) && msg.wParam == VK_TAB) {
                eaten = IsDialogMessageW(root, &msg);
            }
        }
        if (eaten) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

static void release_target(ui_window *w) {
    if (w->brush != NULL) {
        ID2D1SolidColorBrush_Release(w->brush);
        w->brush = NULL;
    }
    if (w->target != NULL) {
        ID2D1HwndRenderTarget_Release(w->target);
        w->target = NULL;
    }
}

static int ensure_target(ui_window *w) {
    if (w->target != NULL) {
        return 1;
    }
    RECT rc;
    GetClientRect(w->hwnd, &rc);
    D2D1_RENDER_TARGET_PROPERTIES props;
    memset(&props, 0, sizeof(props));
    props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    props.pixelFormat.format = DXGI_FORMAT_UNKNOWN;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_UNKNOWN;
    props.dpiX = (float)w->dpi;
    props.dpiY = (float)w->dpi;
    D2D1_HWND_RENDER_TARGET_PROPERTIES hprops;
    hprops.hwnd = w->hwnd;
    hprops.pixelSize.width = (UINT32)(rc.right - rc.left);
    hprops.pixelSize.height = (UINT32)(rc.bottom - rc.top);
    hprops.presentOptions = D2D1_PRESENT_OPTIONS_NONE;
    if (FAILED(ID2D1Factory_CreateHwndRenderTarget(d2d, &props, &hprops, &w->target))) {
        w->target = NULL;
        return 0;
    }
    D2D1_COLOR_F white = colour_of(0xFFFFFF, 1.0f);
    if (FAILED(ID2D1HwndRenderTarget_CreateSolidColorBrush(w->target, &white, NULL, &w->brush))) {
        release_target(w);
        return 0;
    }
    return 1;
}

static void update_size(ui_window *w) {
    RECT rc;
    GetClientRect(w->hwnd, &rc);
    w->width = (float)(rc.right - rc.left) / w->scale;
    w->height = (float)(rc.bottom - rc.top) / w->scale;
    if (w->target != NULL) {
        D2D1_SIZE_U size;
        size.width = (UINT32)(rc.right - rc.left);
        size.height = (UINT32)(rc.bottom - rc.top);
        ID2D1HwndRenderTarget_Resize(w->target, &size);
    }
}

static void paint(ui_window *w) {
    PAINTSTRUCT ps;
    BeginPaint(w->hwnd, &ps);
    EndPaint(w->hwnd, &ps);
    if (!ensure_target(w)) {
        return;
    }
    ID2D1RenderTarget *rt = (ID2D1RenderTarget *)w->target;
    ID2D1RenderTarget_BeginDraw(rt);
    D2D1_MATRIX_3X2_F identity = {{{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}}};
    ID2D1RenderTarget_SetTransform(rt, &identity);
    D2D1_COLOR_F bg = colour_of(w->theme->bg, 1.0f);
    ID2D1RenderTarget_Clear(rt, &bg);
    layer_depth = 0;
    if (w->on->paint != NULL) {
        w->on->paint(w);
    }
    while (layer_depth > 0) {
        ui_pop_layer(w);
    }
    HRESULT hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    if (hr == D2DERR_RECREATE_TARGET) {
        release_target(w);
        InvalidateRect(w->hwnd, NULL, FALSE);
    }
}

static void resize_for_dpi(ui_window *w, float width, float height) {
    RECT rc = {0, 0, ui_px(w, width), ui_px(w, height)};
    DWORD style = (DWORD)GetWindowLongPtrW(w->hwnd, GWL_STYLE);
    DWORD ex = (DWORD)GetWindowLongPtrW(w->hwnd, GWL_EXSTYLE);
    AdjustWindowRectExForDpi(&rc, style, FALSE, ex, w->dpi);
    SetWindowPos(w->hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        ui_window *created = (ui_window *)cs->lpCreateParams;
        created->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)created);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    ui_window *w = (ui_window *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (w == NULL) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (w->on->message != NULL) {
        int handled = 0;
        LRESULT result = w->on->message(w, msg, wp, lp, &handled);
        if (handled) {
            return result;
        }
    }
    switch (msg) {
        case WM_PAINT:
            paint(w);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            update_size(w);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_DPICHANGED: {
            float width = w->width;
            float height = w->height;
            w->dpi = HIWORD(wp);
            w->scale = (float)w->dpi / 96.0f;
            RECT *suggested = (RECT *)lp;
            SetWindowPos(hwnd, NULL, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            if (w->target != NULL) {
                ID2D1HwndRenderTarget_SetDpi(w->target, (float)w->dpi, (float)w->dpi);
            }
            resize_for_dpi(w, width, height);
            update_size(w);
            if (w->on->dpi_changed != NULL) {
                w->on->dpi_changed(w);
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!w->tracking) {
                TRACKMOUSEEVENT tme;
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                tme.dwHoverTime = 0;
                TrackMouseEvent(&tme);
                w->tracking = 1;
            }
            if (w->on->mouse_move != NULL) {
                w->on->mouse_move(w, (float)GET_X_LPARAM(lp) / w->scale, (float)GET_Y_LPARAM(lp) / w->scale);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            w->tracking = 0;
            if (w->on->mouse_leave != NULL) {
                w->on->mouse_leave(w);
            }
            return 0;
        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            if (w->on->mouse_down != NULL) {
                w->on->mouse_down(w, (float)GET_X_LPARAM(lp) / w->scale, (float)GET_Y_LPARAM(lp) / w->scale);
            }
            return 0;
        case WM_LBUTTONUP:
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            if (w->on->mouse_up != NULL) {
                w->on->mouse_up(w, (float)GET_X_LPARAM(lp) / w->scale, (float)GET_Y_LPARAM(lp) / w->scale);
            }
            return 0;
        case WM_MOUSEWHEEL: {
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            if (w->on->wheel != NULL) {
                w->on->wheel(w, (float)pt.x / w->scale, (float)pt.y / w->scale, GET_WHEEL_DELTA_WPARAM(wp));
            }
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(w->hand_cursor ? 32649 : 32512)));
                return TRUE;
            }
            break;
        case WM_TIMER:
            if (w->on->timer != NULL) {
                w->on->timer(w, (UINT_PTR)wp);
            }
            return 0;
        case UI_WM_PROGRESS: {
            ui_job *job = (ui_job *)lp;
            InterlockedExchange(&job->progress_pending, 0);
            if (w->on->job_progress != NULL) {
                w->on->job_progress(w, job, (int64_t)job->done, (int64_t)job->total);
            }
            return 0;
        }
        case UI_WM_JOB: {
            ui_job *job = (ui_job *)lp;
            if (w->on->job_done != NULL) {
                w->on->job_done(w, job);
            }
            ui_job_free(job);
            return 0;
        }
        case WM_CLOSE:
            if (w->on->closing != NULL && !w->on->closing(w)) {
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (w->on->destroyed != NULL) {
                w->on->destroyed(w);
            }
            release_target(w);
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            free(w);
            return DefWindowProcW(hwnd, msg, wp, lp);
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int ui_px(ui_window *w, float dips) {
    float value = dips * w->scale;
    return (int)(value + 0.5f);
}

ui_window *ui_window_create(const wchar_t *title, float width, float height, HWND owner,
                            const gs_theme *theme, const ui_handlers *on, void *self) {
    ui_window *w = (ui_window *)calloc(1, sizeof(ui_window));
    if (w == NULL) {
        return NULL;
    }
    w->theme = theme;
    w->on = on;
    w->self = self;
    w->dpi = owner != NULL ? GetDpiForWindow(owner) : GetDpiForSystem();
    if (w->dpi == 0) {
        w->dpi = 96;
    }
    w->scale = (float)w->dpi / 96.0f;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT rc = {0, 0, ui_px(w, width), ui_px(w, height)};
    AdjustWindowRectExForDpi(&rc, style, FALSE, 0, w->dpi);
    HWND hwnd = CreateWindowExW(0, UI_CLASS, title, style, CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top, owner, NULL,
                                GetModuleHandleW(NULL), w);
    if (hwnd == NULL) {
        free(w);
        return NULL;
    }
    UINT actual = GetDpiForWindow(hwnd);
    if (actual != 0 && actual != w->dpi) {
        w->dpi = actual;
        w->scale = (float)actual / 96.0f;
        resize_for_dpi(w, width, height);
    }
    update_size(w);
    return w;
}

void ui_invalidate(ui_window *w) {
    if (w != NULL && w->hwnd != NULL) {
        InvalidateRect(w->hwnd, NULL, FALSE);
    }
}

void ui_set_title(ui_window *w, const wchar_t *title) {
    SetWindowTextW(w->hwnd, title);
}

void ui_bring_forward(HWND hwnd) {
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }
    ShowWindow(hwnd, SW_SHOW);
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
}

void ui_keep_front(HWND hwnd) {
    if (hwnd != NULL && IsWindow(hwnd)) {
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
    }
}

ui_font *ui_font_get(const wchar_t *family, float points, int bold) {
    for (int i = 0; i < font_count; i++) {
        if (fonts[i].points == points && fonts[i].bold == bold && wcscmp(fonts[i].family, family) == 0) {
            return &fonts[i];
        }
    }
    if (font_count >= UI_MAX_FONTS) {
        return &fonts[0];
    }
    ui_font *f = &fonts[font_count];
    memset(f, 0, sizeof(*f));
    wcsncpy(f->family, family, 31);
    f->points = points;
    f->bold = bold;
    float size = points * 96.0f / 72.0f;
    DWRITE_FONT_WEIGHT weight = bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    if (FAILED(IDWriteFactory_CreateTextFormat(dwrite, family, NULL, weight, DWRITE_FONT_STYLE_NORMAL,
                                               DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &f->format))) {
        f->format = NULL;
    }
    f->line_space = size * 1.2f;
    IDWriteFontCollection *collection = NULL;
    if (SUCCEEDED(IDWriteFactory_GetSystemFontCollection(dwrite, &collection, FALSE))) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        IDWriteFontCollection_FindFamilyName(collection, family, &index, &exists);
        IDWriteFontFamily *fam = NULL;
        if (exists && SUCCEEDED(IDWriteFontCollection_GetFontFamily(collection, index, &fam))) {
            IDWriteFont *font = NULL;
            if (SUCCEEDED(IDWriteFontFamily_GetFirstMatchingFont(fam, weight, DWRITE_FONT_STRETCH_NORMAL,
                                                                 DWRITE_FONT_STYLE_NORMAL, &font))) {
                DWRITE_FONT_METRICS m;
                IDWriteFont_GetMetrics(font, &m);
                if (m.designUnitsPerEm > 0) {
                    float space = (float)(m.ascent + m.descent) * size / (float)m.designUnitsPerEm;
                    f->line_space = (float)(int)(space + 0.999f);
                }
                IDWriteFont_Release(font);
            }
            IDWriteFontFamily_Release(fam);
        }
        IDWriteFontCollection_Release(collection);
    }
    font_count++;
    return f;
}

static IDWriteTextLayout *make_layout(ui_font *font, const wchar_t *text, int len, float wrap, int centered) {
    if (font == NULL || font->format == NULL) {
        return NULL;
    }
    IDWriteTextLayout *layout = NULL;
    float width = wrap > 0.0f ? wrap : 100000.0f;
    if (FAILED(IDWriteFactory_CreateTextLayout(dwrite, text, (UINT32)len, font->format, width, 100000.0f,
                                               &layout))) {
        return NULL;
    }
    IDWriteTextLayout_SetWordWrapping(layout, wrap > 0.0f ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    IDWriteTextLayout_SetTextAlignment(layout, centered ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
    return layout;
}

float ui_measure(ui_font *font, const wchar_t *text, int len) {
    if (len <= 0) {
        return 0.0f;
    }
    IDWriteTextLayout *layout = make_layout(font, text, len, 0.0f, 0);
    if (layout == NULL) {
        return 0.0f;
    }
    DWRITE_TEXT_METRICS m;
    float width = 0.0f;
    if (SUCCEEDED(IDWriteTextLayout_GetMetrics(layout, &m))) {
        width = m.widthIncludingTrailingWhitespace;
    }
    IDWriteTextLayout_Release(layout);
    return width;
}

static ID2D1RenderTarget *rt_of(ui_window *w) {
    return (ID2D1RenderTarget *)w->target;
}

static void set_brush(ui_window *w, uint32_t rgb, float alpha) {
    D2D1_COLOR_F c = colour_of(rgb, alpha);
    ID2D1SolidColorBrush_SetColor(w->brush, &c);
}

void ui_fill_rect_alpha(ui_window *w, float x0, float y0, float x1, float y1, uint32_t rgb, float alpha) {
    if (w->target == NULL || x1 <= x0 || y1 <= y0) {
        return;
    }
    set_brush(w, rgb, alpha);
    D2D1_RECT_F r = {x0, y0, x1, y1};
    ID2D1RenderTarget_SetAntialiasMode(rt_of(w), D2D1_ANTIALIAS_MODE_ALIASED);
    ID2D1RenderTarget_FillRectangle(rt_of(w), &r, (ID2D1Brush *)w->brush);
    ID2D1RenderTarget_SetAntialiasMode(rt_of(w), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void ui_fill_rect(ui_window *w, float x0, float y0, float x1, float y1, uint32_t rgb) {
    ui_fill_rect_alpha(w, x0, y0, x1, y1, rgb, 1.0f);
}

void ui_stroke_rect(ui_window *w, float x0, float y0, float x1, float y1, uint32_t rgb, float width) {
    if (w->target == NULL) {
        return;
    }
    set_brush(w, rgb, 1.0f);
    float half = width / 2.0f;
    D2D1_RECT_F r = {x0 + half, y0 + half, x1 - half, y1 - half};
    ID2D1RenderTarget_SetAntialiasMode(rt_of(w), D2D1_ANTIALIAS_MODE_ALIASED);
    ID2D1RenderTarget_DrawRectangle(rt_of(w), &r, (ID2D1Brush *)w->brush, width, NULL);
    ID2D1RenderTarget_SetAntialiasMode(rt_of(w), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void ui_line(ui_window *w, float x0, float y0, float x1, float y1, uint32_t rgb, float width) {
    if (w->target == NULL) {
        return;
    }
    set_brush(w, rgb, 1.0f);
    D2D1_POINT_2F a = {x0, y0 + 0.5f};
    D2D1_POINT_2F b = {x1, y1 + 0.5f};
    ID2D1RenderTarget_SetAntialiasMode(rt_of(w), D2D1_ANTIALIAS_MODE_ALIASED);
    ID2D1RenderTarget_DrawLine(rt_of(w), a, b, (ID2D1Brush *)w->brush, width, NULL);
    ID2D1RenderTarget_SetAntialiasMode(rt_of(w), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void ui_fill_ellipse(ui_window *w, float x0, float y0, float x1, float y1, uint32_t rgb) {
    if (w->target == NULL) {
        return;
    }
    set_brush(w, rgb, 1.0f);
    D2D1_ELLIPSE e;
    e.point.x = (x0 + x1) / 2.0f;
    e.point.y = (y0 + y1) / 2.0f;
    e.radiusX = (x1 - x0) / 2.0f;
    e.radiusY = (y1 - y0) / 2.0f;
    ID2D1RenderTarget_FillEllipse(rt_of(w), &e, (ID2D1Brush *)w->brush);
}

static ID2D1PathGeometry *polygon_geometry(const float *points, int count) {
    ID2D1PathGeometry *geo = NULL;
    if (count < 3 || FAILED(ID2D1Factory_CreatePathGeometry(d2d, &geo))) {
        return NULL;
    }
    ID2D1GeometrySink *sink = NULL;
    if (FAILED(ID2D1PathGeometry_Open(geo, &sink))) {
        ID2D1PathGeometry_Release(geo);
        return NULL;
    }
    D2D1_POINT_2F start = {points[0], points[1]};
    ID2D1GeometrySink_BeginFigure(sink, start, D2D1_FIGURE_BEGIN_FILLED);
    for (int i = 1; i < count; i++) {
        D2D1_POINT_2F p = {points[i * 2], points[i * 2 + 1]};
        ID2D1GeometrySink_AddLine(sink, p);
    }
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    ID2D1GeometrySink_Close(sink);
    ID2D1GeometrySink_Release(sink);
    return geo;
}

void ui_fill_half_ellipse(ui_window *w, float x0, float y0, float x1, float y1, uint32_t rgb) {
    if (w->target == NULL || x1 <= x0 || y1 <= y0) {
        return;
    }
    ID2D1PathGeometry *geo = NULL;
    if (FAILED(ID2D1Factory_CreatePathGeometry(d2d, &geo))) {
        return;
    }
    ID2D1GeometrySink *sink = NULL;
    if (SUCCEEDED(ID2D1PathGeometry_Open(geo, &sink))) {
        float cy = (y0 + y1) / 2.0f;
        D2D1_POINT_2F start = {x0, cy};
        ID2D1GeometrySink_BeginFigure(sink, start, D2D1_FIGURE_BEGIN_FILLED);
        D2D1_ARC_SEGMENT arc;
        arc.point.x = x1;
        arc.point.y = cy;
        arc.size.width = (x1 - x0) / 2.0f;
        arc.size.height = (y1 - y0) / 2.0f;
        arc.rotationAngle = 0.0f;
        arc.sweepDirection = D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
        arc.arcSize = D2D1_ARC_SIZE_SMALL;
        ID2D1GeometrySink_AddArc(sink, &arc);
        ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
        ID2D1GeometrySink_Close(sink);
        ID2D1GeometrySink_Release(sink);
        set_brush(w, rgb, 1.0f);
        ID2D1RenderTarget_FillGeometry(rt_of(w), (ID2D1Geometry *)geo, (ID2D1Brush *)w->brush, NULL);
    }
    ID2D1PathGeometry_Release(geo);
}

void ui_fill_polygon(ui_window *w, const float *points, int count, uint32_t rgb, float alpha) {
    if (w->target == NULL) {
        return;
    }
    ID2D1PathGeometry *geo = polygon_geometry(points, count);
    if (geo == NULL) {
        return;
    }
    set_brush(w, rgb, alpha);
    ID2D1RenderTarget_FillGeometry(rt_of(w), (ID2D1Geometry *)geo, (ID2D1Brush *)w->brush, NULL);
    ID2D1PathGeometry_Release(geo);
}

int ui_push_polygon_clip(ui_window *w, const float *points, int count) {
    if (w->target == NULL || layer_depth >= UI_LAYER_DEPTH) {
        return 0;
    }
    ID2D1PathGeometry *geo = polygon_geometry(points, count);
    if (geo == NULL) {
        return 0;
    }
    ID2D1Layer *layer = NULL;
    if (FAILED(ID2D1RenderTarget_CreateLayer(rt_of(w), NULL, &layer))) {
        ID2D1PathGeometry_Release(geo);
        return 0;
    }
    D2D1_LAYER_PARAMETERS params;
    memset(&params, 0, sizeof(params));
    params.contentBounds.left = -1.0e9f;
    params.contentBounds.top = -1.0e9f;
    params.contentBounds.right = 1.0e9f;
    params.contentBounds.bottom = 1.0e9f;
    params.geometricMask = (ID2D1Geometry *)geo;
    params.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    params.maskTransform._11 = 1.0f;
    params.maskTransform._22 = 1.0f;
    params.opacity = 1.0f;
    params.opacityBrush = NULL;
    params.layerOptions = D2D1_LAYER_OPTIONS_NONE;
    ID2D1RenderTarget_PushLayer(rt_of(w), &params, layer);
    layer_stack[layer_depth] = layer;
    geometry_stack[layer_depth] = geo;
    layer_depth++;
    return 1;
}

void ui_pop_layer(ui_window *w) {
    if (layer_depth <= 0) {
        return;
    }
    layer_depth--;
    ID2D1RenderTarget_PopLayer(rt_of(w));
    ID2D1Layer_Release(layer_stack[layer_depth]);
    ID2D1PathGeometry_Release(geometry_stack[layer_depth]);
}

void ui_push_clip(ui_window *w, float x0, float y0, float x1, float y1) {
    if (w->target == NULL) {
        return;
    }
    D2D1_RECT_F r = {x0, y0, x1, y1};
    ID2D1RenderTarget_PushAxisAlignedClip(rt_of(w), &r, D2D1_ANTIALIAS_MODE_ALIASED);
}

void ui_pop_clip(ui_window *w) {
    if (w->target != NULL) {
        ID2D1RenderTarget_PopAxisAlignedClip(rt_of(w));
    }
}

void ui_text(ui_window *w, float x, float y, const wchar_t *text, int len, ui_font *font,
             uint32_t rgb, ui_anchor anchor, float wrap, int centered) {
    if (w->target == NULL || text == NULL || len <= 0) {
        return;
    }
    IDWriteTextLayout *layout = make_layout(font, text, len, wrap, centered);
    if (layout == NULL) {
        return;
    }
    DWRITE_TEXT_METRICS m;
    if (FAILED(IDWriteTextLayout_GetMetrics(layout, &m))) {
        IDWriteTextLayout_Release(layout);
        return;
    }
    float ax = 0.0f;
    float ay = 0.0f;
    switch (anchor) {
        case UI_N: ax = 0.5f; break;
        case UI_NE: ax = 1.0f; break;
        case UI_W: ay = 0.5f; break;
        case UI_CENTER: ax = 0.5f; ay = 0.5f; break;
        case UI_E: ax = 1.0f; ay = 0.5f; break;
        case UI_SW: ay = 1.0f; break;
        case UI_S: ax = 0.5f; ay = 1.0f; break;
        case UI_SE: ax = 1.0f; ay = 1.0f; break;
        default: break;
    }
    D2D1_POINT_2F origin;
    origin.x = x - m.left - ax * m.width;
    origin.y = y - m.top - ay * m.height;
    set_brush(w, rgb, 1.0f);
    ID2D1RenderTarget_DrawTextLayout(rt_of(w), origin, layout, (ID2D1Brush *)w->brush,
                                     D2D1_DRAW_TEXT_OPTIONS_NONE);
    IDWriteTextLayout_Release(layout);
}

void ui_text_utf8(ui_window *w, float x, float y, const char *text, ui_font *font, uint32_t rgb,
                  ui_anchor anchor, float wrap, int centered) {
    wchar_t *wide = ui_wide(text);
    if (wide != NULL) {
        ui_text(w, x, y, wide, (int)wcslen(wide), font, rgb, anchor, wrap, centered);
        free(wide);
    }
}

static ui_image *image_from_source(IWICBitmapSource *source, char *why, size_t room) {
    UINT width = 0;
    UINT height = 0;
    IWICBitmapSource_GetSize(source, &width, &height);
    if (width == 0 || height == 0) {
        snprintf(why, room, "the image is empty");
        return NULL;
    }
    ui_image *image = (ui_image *)calloc(1, sizeof(ui_image));
    unsigned char *pixels = (unsigned char *)malloc((size_t)width * height * 4);
    if (image == NULL || pixels == NULL) {
        free(image);
        free(pixels);
        snprintf(why, room, "out of memory");
        return NULL;
    }
    HRESULT hr = IWICBitmapSource_CopyPixels(source, NULL, width * 4, width * height * 4, pixels);
    if (FAILED(hr)) {
        free(image);
        free(pixels);
        snprintf(why, room, "couldnt read the pixels (0x%08lX)", (unsigned long)hr);
        return NULL;
    }
    image->width = (int)width;
    image->height = (int)height;
    image->pixels = pixels;
    return image;
}

static ui_image *decode_as(const void *data, size_t len, const GUID *format, char *why, size_t room) {
    IWICStream *stream = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *convert = NULL;
    ui_image *image = NULL;
    HRESULT hr = IWICImagingFactory_CreateStream(wic, &stream);
    if (SUCCEEDED(hr)) {
        hr = IWICStream_InitializeFromMemory(stream, (BYTE *)data, (DWORD)len);
    }
    if (SUCCEEDED(hr)) {
        hr = IWICImagingFactory_CreateDecoderFromStream(wic, (IStream *)stream, NULL,
                                                        WICDecodeMetadataCacheOnDemand, &decoder);
    }
    if (SUCCEEDED(hr)) {
        hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    }
    if (SUCCEEDED(hr)) {
        hr = IWICImagingFactory_CreateFormatConverter(wic, &convert);
    }
    if (SUCCEEDED(hr)) {
        hr = IWICFormatConverter_Initialize(convert, (IWICBitmapSource *)frame, format,
                                            WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeMedianCut);
    }
    if (SUCCEEDED(hr)) {
        image = image_from_source((IWICBitmapSource *)convert, why, room);
    } else {
        snprintf(why, room, "Windows couldnt decode it (0x%08lX)", (unsigned long)hr);
    }
    if (convert != NULL) {
        IWICFormatConverter_Release(convert);
    }
    if (frame != NULL) {
        IWICBitmapFrameDecode_Release(frame);
    }
    if (decoder != NULL) {
        IWICBitmapDecoder_Release(decoder);
    }
    if (stream != NULL) {
        IWICStream_Release(stream);
    }
    return image;
}

ui_image *ui_image_decode(const void *data, size_t len, char *why, size_t room) {
    return decode_as(data, len, &GUID_WICPixelFormat32bppPBGRA, why, room);
}

ui_image *ui_image_decode_rgba(const void *data, size_t len, char *why, size_t room) {
    return decode_as(data, len, &GUID_WICPixelFormat32bppRGBA, why, room);
}

ui_image *ui_image_from_rgba(const unsigned char *rgba, int width, int height) {
    ui_image *image = (ui_image *)calloc(1, sizeof(ui_image));
    unsigned char *pixels = (unsigned char *)malloc((size_t)width * height * 4);
    if (image == NULL || pixels == NULL) {
        free(image);
        free(pixels);
        return NULL;
    }
    for (size_t i = 0; i < (size_t)width * height; i++) {
        unsigned a = rgba[i * 4 + 3];
        pixels[i * 4 + 0] = (unsigned char)((rgba[i * 4 + 2] * a + 127) / 255);
        pixels[i * 4 + 1] = (unsigned char)((rgba[i * 4 + 1] * a + 127) / 255);
        pixels[i * 4 + 2] = (unsigned char)((rgba[i * 4 + 0] * a + 127) / 255);
        pixels[i * 4 + 3] = (unsigned char)a;
    }
    image->width = width;
    image->height = height;
    image->pixels = pixels;
    return image;
}

ui_image *ui_image_scaled(const ui_image *src, int width, int height) {
    if (src == NULL || width <= 0 || height <= 0) {
        return NULL;
    }
    IWICBitmap *bitmap = NULL;
    IWICBitmapScaler *scaler = NULL;
    ui_image *image = NULL;
    char why[128];
    HRESULT hr = IWICImagingFactory_CreateBitmapFromMemory(
        wic, (UINT)src->width, (UINT)src->height, &GUID_WICPixelFormat32bppPBGRA, (UINT)src->width * 4,
        (UINT)(src->width * src->height * 4), src->pixels, &bitmap);
    if (SUCCEEDED(hr)) {
        hr = IWICImagingFactory_CreateBitmapScaler(wic, &scaler);
    }
    if (SUCCEEDED(hr)) {
        hr = IWICBitmapScaler_Initialize(scaler, (IWICBitmapSource *)bitmap, (UINT)width, (UINT)height,
                                         (WICBitmapInterpolationMode)4);
    }
    if (SUCCEEDED(hr)) {
        image = image_from_source((IWICBitmapSource *)scaler, why, sizeof(why));
    }
    if (scaler != NULL) {
        IWICBitmapScaler_Release(scaler);
    }
    if (bitmap != NULL) {
        IWICBitmap_Release(bitmap);
    }
    return image;
}

int ui_resource_bytes(int id, const void **data, size_t *len) {
    HRSRC found = FindResourceW(NULL, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
    if (found == NULL) {
        return 0;
    }
    HGLOBAL loaded = LoadResource(NULL, found);
    if (loaded == NULL) {
        return 0;
    }
    *data = LockResource(loaded);
    *len = SizeofResource(NULL, found);
    return *data != NULL && *len > 0;
}

ui_image *ui_image_resource(int id) {
    const void *data = NULL;
    size_t len = 0;
    char why[128];
    if (!ui_resource_bytes(id, &data, &len)) {
        return NULL;
    }
    return ui_image_decode(data, len, why, sizeof(why));
}

void ui_image_free(ui_image *image) {
    if (image == NULL) {
        return;
    }
    if (image->bitmap != NULL) {
        ID2D1Bitmap_Release(image->bitmap);
    }
    free(image->pixels);
    free(image);
}

void ui_draw_image(ui_window *w, ui_image *image, float x0, float y0, float x1, float y1) {
    if (w->target == NULL || image == NULL) {
        return;
    }
    if (image->bitmap != NULL && image->owner != (void *)w->target) {
        ID2D1Bitmap_Release(image->bitmap);
        image->bitmap = NULL;
    }
    if (image->bitmap == NULL) {
        D2D1_BITMAP_PROPERTIES props;
        props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        props.dpiX = 96.0f;
        props.dpiY = 96.0f;
        D2D1_SIZE_U size = {(UINT32)image->width, (UINT32)image->height};
        if (FAILED(ID2D1RenderTarget_CreateBitmap(rt_of(w), size, image->pixels, (UINT32)image->width * 4,
                                                  &props, &image->bitmap))) {
            image->bitmap = NULL;
            return;
        }
        image->owner = (void *)w->target;
    }
    D2D1_RECT_F dest = {x0, y0, x1, y1};
    ID2D1RenderTarget_DrawBitmap(rt_of(w), image->bitmap, &dest, 1.0f,
                                 D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, NULL);
}

int ui_image_decodes(const char *path) {
    buf data;
    buf_init(&data);
    int ok = 0;
    if (file_read_all(path, &data) && data.len > 0) {
        char why[128];
        ui_image *image = ui_image_decode(data.data, data.len, why, sizeof(why));
        ok = image != NULL;
        ui_image_free(image);
    }
    buf_free(&data);
    return ok;
}

void ui_message(HWND owner, const char *title, const char *text, UINT icon) {
    wchar_t *wt = ui_wide(title);
    wchar_t *wx = ui_wide(text);
    MessageBoxW(owner, wx, wt, MB_OK | icon);
    free(wt);
    free(wx);
}

int ui_ask_yes_no(HWND owner, const char *title, const char *text) {
    wchar_t *wt = ui_wide(title);
    wchar_t *wx = ui_wide(text);
    int answer = MessageBoxW(owner, wx, wt, MB_YESNO | MB_ICONQUESTION);
    free(wt);
    free(wx);
    return answer == IDYES;
}

static char *item_path(IShellItem *item) {
    PWSTR wide = NULL;
    char *out = NULL;
    if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &wide))) {
        out = wide_to_utf8(wide);
        CoTaskMemFree(wide);
    }
    return out;
}

static IFileOpenDialog *open_dialog(const char *title, const char *initial_dir, DWORD extra) {
    IFileOpenDialog *dlg = NULL;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog,
                                (void **)&dlg))) {
        return NULL;
    }
    DWORD options = 0;
    IFileOpenDialog_GetOptions(dlg, &options);
    IFileOpenDialog_SetOptions(dlg, options | FOS_FORCEFILESYSTEM | extra);
    if (title != NULL) {
        wchar_t *wt = ui_wide(title);
        IFileOpenDialog_SetTitle(dlg, wt);
        free(wt);
    }
    if (initial_dir != NULL && initial_dir[0] != 0 && path_is_dir(initial_dir)) {
        wchar_t *wd = ui_wide(initial_dir);
        for (wchar_t *c = wd; *c; c++) {
            if (*c == L'/') {
                *c = L'\\';
            }
        }
        IShellItem *folder = NULL;
        if (SUCCEEDED(SHCreateItemFromParsingName(wd, NULL, &IID_IShellItem, (void **)&folder))) {
            IFileOpenDialog_SetFolder(dlg, folder);
            IShellItem_Release(folder);
        }
        free(wd);
    }
    return dlg;
}

char *ui_pick_folder(HWND owner, const char *title, const char *initial) {
    IFileOpenDialog *dlg = open_dialog(title, initial, FOS_PICKFOLDERS);
    if (dlg == NULL) {
        return NULL;
    }
    char *out = NULL;
    if (SUCCEEDED(IFileOpenDialog_Show(dlg, owner))) {
        IShellItem *item = NULL;
        if (SUCCEEDED(IFileOpenDialog_GetResult(dlg, &item))) {
            out = item_path(item);
            IShellItem_Release(item);
        }
    }
    IFileOpenDialog_Release(dlg);
    return out;
}

static void set_filter(IFileOpenDialog *dlg, const wchar_t *name, const wchar_t *spec) {
    if (name == NULL || spec == NULL) {
        return;
    }
    COMDLG_FILTERSPEC specs[2];
    specs[0].pszName = name;
    specs[0].pszSpec = spec;
    specs[1].pszName = L"All files";
    specs[1].pszSpec = L"*.*";
    IFileOpenDialog_SetFileTypes(dlg, 2, specs);
}

char *ui_pick_file(HWND owner, const char *title, const char *initial_dir, const char *initial_name,
                   const wchar_t *filter_name, const wchar_t *filter_spec) {
    IFileOpenDialog *dlg = open_dialog(title, initial_dir, FOS_FILEMUSTEXIST);
    if (dlg == NULL) {
        return NULL;
    }
    set_filter(dlg, filter_name, filter_spec);
    if (initial_name != NULL && initial_name[0] != 0) {
        wchar_t *wn = ui_wide(initial_name);
        IFileOpenDialog_SetFileName(dlg, wn);
        free(wn);
    }
    char *out = NULL;
    if (SUCCEEDED(IFileOpenDialog_Show(dlg, owner))) {
        IShellItem *item = NULL;
        if (SUCCEEDED(IFileOpenDialog_GetResult(dlg, &item))) {
            out = item_path(item);
            IShellItem_Release(item);
        }
    }
    IFileOpenDialog_Release(dlg);
    return out;
}

int ui_pick_files(HWND owner, const char *title, const wchar_t *filter_name,
                  const wchar_t *filter_spec, char ***out, size_t *count) {
    *out = NULL;
    *count = 0;
    IFileOpenDialog *dlg = open_dialog(title, NULL, FOS_FILEMUSTEXIST | FOS_ALLOWMULTISELECT);
    if (dlg == NULL) {
        return 0;
    }
    set_filter(dlg, filter_name, filter_spec);
    if (SUCCEEDED(IFileOpenDialog_Show(dlg, owner))) {
        IShellItemArray *items = NULL;
        if (SUCCEEDED(IFileOpenDialog_GetResults(dlg, &items))) {
            DWORD n = 0;
            IShellItemArray_GetCount(items, &n);
            char **paths = (char **)calloc(n + 1, sizeof(char *));
            for (DWORD i = 0; paths != NULL && i < n; i++) {
                IShellItem *item = NULL;
                if (SUCCEEDED(IShellItemArray_GetItemAt(items, i, &item))) {
                    char *path = item_path(item);
                    if (path != NULL) {
                        paths[(*count)++] = path;
                    }
                    IShellItem_Release(item);
                }
            }
            *out = paths;
            IShellItemArray_Release(items);
        }
    }
    IFileOpenDialog_Release(dlg);
    return *count > 0;
}

void ui_open_explorer(const char *path) {
    char *quoted = (char *)malloc(strlen(path) + 3);
    if (quoted == NULL) {
        return;
    }
    snprintf(quoted, strlen(path) + 3, "\"%s\"", path);
    path_to_backslash(quoted);
    wchar_t *args = ui_wide(quoted);
    ShellExecuteW(NULL, L"open", L"explorer.exe", args, NULL, SW_SHOWNORMAL);
    free(args);
    free(quoted);
}

ui_job *ui_job_new(HWND hwnd, int kind, int serial, ui_job_fn run, ui_job_free_fn release, void *data) {
    ui_job *job = (ui_job *)calloc(1, sizeof(ui_job));
    if (job == NULL) {
        return NULL;
    }
    job->ctx.id = (int64_t)(intptr_t)job;
    job->ctx.cancel = 0;
    job->hwnd = hwnd;
    job->kind = kind;
    job->serial = serial;
    job->run = run;
    job->release = release;
    job->data = data;
    err_clear(&job->e);
    return job;
}

static DWORD WINAPI job_thread(LPVOID param) {
    ui_job *job = (ui_job *)param;
    if (job->serial) {
        AcquireSRWLockExclusive(&backend_lock);
    }
    job->ok = job->run(job);
    if (job->serial) {
        ReleaseSRWLockExclusive(&backend_lock);
    }
    if (!job->ok && !job->e.set) {
        err_set(&job->e, "Unknown failure");
    }
    if (!PostMessageW(job->hwnd, UI_WM_JOB, 0, (LPARAM)job)) {
        ui_job_free(job);
    }
    return 0;
}

int ui_job_start(ui_job *job) {
    HANDLE thread = CreateThread(NULL, 0, job_thread, job, 0, NULL);
    if (thread == NULL) {
        return 0;
    }
    CloseHandle(thread);
    return 1;
}

void ui_job_report(ui_job *job, int64_t done, int64_t total) {
    InterlockedExchange64(&job->done, done);
    InterlockedExchange64(&job->total, total);
    if (InterlockedExchange(&job->progress_pending, 1) == 0) {
        if (!PostMessageW(job->hwnd, UI_WM_PROGRESS, 0, (LPARAM)job)) {
            InterlockedExchange(&job->progress_pending, 0);
        }
    }
}

void ui_job_free(ui_job *job) {
    if (job == NULL) {
        return;
    }
    if (job->release != NULL) {
        job->release(job);
    }
    free(job);
}
