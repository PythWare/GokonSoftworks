#define WIN32_LEAN_AND_MEAN
#include "diag.h"
#include "gsfs.h"
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const protected_roots[][2] = {
    {"Program Files", "Program Files"},
    {"Program Files (x86)", "Program Files (x86)"},
    {"Windows", "the Windows folder"},
    {"System32", "System32"},
};

static void note(gs_diagnostics *d, const char *severity, const char *fmt, ...) {
    diag_issue *bigger = (diag_issue *)realloc(d->issues, (d->issue_count + 1) * sizeof(diag_issue));
    if (bigger == NULL) {
        return;
    }
    d->issues = bigger;
    diag_issue *issue = &d->issues[d->issue_count++];
    snprintf(issue->severity, sizeof(issue->severity), "%s", severity);
    va_list args;
    va_start(args, fmt);
    vsnprintf(issue->message, sizeof(issue->message), fmt, args);
    va_end(args);
}

static void win_reason(DWORD code, char *out, size_t room) {
    char text[256];
    text[0] = 0;
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, text,
                   sizeof(text), NULL);
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r' || text[len - 1] == ' ')) {
        text[--len] = 0;
    }
    snprintf(out, room, "[WinError %lu] %s", (unsigned long)code, text);
}

const char *diag_protected_location(const char *path) {
    char *normal = gs_path_normalize(path);
    if (normal == NULL) {
        return "";
    }
    const char *label = "";
    for (size_t r = 0; r < sizeof(protected_roots) / sizeof(protected_roots[0]) && label[0] == 0; r++) {
        size_t len = strlen(normal);
        size_t i = 0;
        while (i <= len) {
            size_t start = i;
            while (i < len && normal[i] != '\\') {
                i++;
            }
            char saved = normal[i];
            normal[i] = 0;
            if (gs_casefold_equal(normal + start, protected_roots[r][0])) {
                label = protected_roots[r][1];
            }
            normal[i] = saved;
            if (label[0] != 0) {
                break;
            }
            i++;
        }
    }
    free(normal);
    return label;
}

static int path_exists(const char *path) {
    wchar_t *wide = path_to_wide(path);
    if (wide == NULL) {
        return 0;
    }
    DWORD attrs = GetFileAttributesW(wide);
    free(wide);
    return attrs != INVALID_FILE_ATTRIBUTES;
}

static char *nearest_existing(const char *path) {
    char *candidate = gs_path_full(path);
    while (candidate != NULL && !path_exists(candidate)) {
        char *parent = gs_path_parent(candidate);
        if (parent == NULL || strcmp(parent, candidate) == 0) {
            free(parent);
            break;
        }
        free(candidate);
        candidate = parent;
    }
    return candidate;
}

static char *drive_of(const char *base) {
    size_t len = strlen(base);
    size_t cut = 0;
    if (len >= 2 && (base[0] == '\\' || base[0] == '/') && (base[1] == '\\' || base[1] == '/')) {
        const char *a = strpbrk(base + 2, "\\/");
        const char *b = a == NULL ? NULL : strpbrk(a + 1, "\\/");
        cut = a == NULL ? 0 : (b == NULL ? len : (size_t)(b - base));
    } else if (len >= 2 && base[1] == ':') {
        cut = 2;
    }
    if (cut == 0) {
        return _strdup(base);
    }
    char *out = (char *)malloc(cut + 1);
    if (out != NULL) {
        memcpy(out, base, cut);
        out[cut] = 0;
    }
    return out;
}

static void random_name(const char *prefix, const char *suffix, char *out, size_t room) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789_";
    static volatile LONG counter;
    uint64_t seed = GetTickCount64() ^ ((uint64_t)GetCurrentProcessId() << 32) ^
                    (uint64_t)InterlockedIncrement(&counter) * 0x9E3779B97F4A7C15ull;
    char middle[9];
    for (int i = 0; i < 8; i++) {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        middle[i] = chars[seed % (sizeof(chars) - 1)];
    }
    middle[8] = 0;
    snprintf(out, room, "%s%s%s", prefix, middle, suffix);
}

static void probe_write(gs_diagnostics *d) {
    char name[64];
    char *probe = NULL;
    HANDLE handle = INVALID_HANDLE_VALUE;
    DWORD failure = 0;
    for (int attempt = 0; attempt < 100 && handle == INVALID_HANDLE_VALUE; attempt++) {
        random_name("gokon_diag_", ".tmp", name, sizeof(name));
        free(probe);
        probe = path_join(d->path, name);
        wchar_t *wide = probe == NULL ? NULL : path_to_wide(probe);
        if (wide == NULL) {
            break;
        }
        handle = CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        failure = handle == INVALID_HANDLE_VALUE ? GetLastError() : 0;
        free(wide);
        if (failure != ERROR_FILE_EXISTS && failure != ERROR_ALREADY_EXISTS) {
            break;
        }
    }
    if (handle == INVALID_HANDLE_VALUE) {
        char why[300];
        win_reason(failure, why, sizeof(why));
        note(d, "error", "Cant write here: %s", why);
        free(probe);
        return;
    }
    DWORD wrote = 0;
    size_t probe_len = strlen(DIAG_PROBE);
    BOOL ok = WriteFile(handle, DIAG_PROBE, (DWORD)probe_len, &wrote, NULL);
    DWORD write_failure = ok ? 0 : GetLastError();
    CloseHandle(handle);
    if (!ok || wrote != probe_len) {
        char why[300];
        win_reason(write_failure, why, sizeof(why));
        note(d, "error", "Cant write here: %s", why);
    } else {
        buf back;
        buf_init(&back);
        if (file_read_all(probe, &back)) {
            d->can_write = back.len == probe_len && memcmp(back.data, DIAG_PROBE, probe_len) == 0;
        } else {
            char why[300];
            win_reason(GetLastError(), why, sizeof(why));
            note(d, "error", "Cant write here: %s", why);
        }
        buf_free(&back);
    }
    if (path_exists(probe)) {
        if (path_delete(probe)) {
            d->can_delete = 1;
        } else {
            char why[300];
            win_reason(GetLastError(), why, sizeof(why));
            note(d, "warning", "Couldnt remove the test file: %s", why);
        }
    }
    free(probe);
}

static void probe_folder(gs_diagnostics *d) {
    char name[64];
    DWORD failure = 0;
    for (int attempt = 0; attempt < 100; attempt++) {
        random_name("gokon_diag_", "", name, sizeof(name));
        char *probe = path_join(d->path, name);
        wchar_t *wide = probe == NULL ? NULL : path_to_wide(probe);
        free(probe);
        if (wide == NULL) {
            failure = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        if (CreateDirectoryW(wide, NULL)) {
            d->can_create = 1;
            RemoveDirectoryW(wide);
            free(wide);
            return;
        }
        failure = GetLastError();
        free(wide);
        if (failure != ERROR_ALREADY_EXISTS) {
            break;
        }
    }
    char why[300];
    win_reason(failure, why, sizeof(why));
    note(d, "error", "Cant create folders here: %s", why);
}

static size_t code_points(const char *text) {
    size_t count = 0;
    for (const unsigned char *c = (const unsigned char *)text; *c; c++) {
        if ((*c & 0xC0u) != 0x80u) {
            count++;
        }
    }
    return count;
}

int diag_run(const char *path, gs_diagnostics *out) {
    memset(out, 0, sizeof(*out));
    out->path = gs_path_normalize(path != NULL && path[0] != 0 ? path : gs_project_root());
    if (out->path == NULL) {
        return 0;
    }
    out->protected_name = diag_protected_location(out->path);

    char *base = nearest_existing(out->path);
    out->drive = base == NULL ? _strdup("") : drive_of(base);
    if (out->drive != NULL && out->drive[0] == 0 && base != NULL) {
        free(out->drive);
        out->drive = _strdup(base);
    }

    wchar_t *wide_base = base == NULL ? NULL : path_to_wide(base);
    ULARGE_INTEGER caller_free;
    ULARGE_INTEGER total;
    ULARGE_INTEGER free_bytes;
    if (wide_base != NULL && GetDiskFreeSpaceExW(wide_base, &caller_free, &total, &free_bytes)) {
        out->have_space = 1;
        out->total_bytes = (int64_t)total.QuadPart;
        out->free_bytes = (int64_t)free_bytes.QuadPart;
    } else {
        char why[300];
        win_reason(GetLastError(), why, sizeof(why));
        note(out, "error", "couldnnt read drive storage: %s", why);
    }
    free(wide_base);
    free(base);

    if (!path_exists(out->path)) {
        note(out, "error", "The software folder doesnt exist.");
        return 1;
    }
    if (!path_is_dir(out->path)) {
        note(out, "error", "The software path isnt a folder.");
        return 1;
    }

    char *pattern = path_join(out->path, "*");
    wchar_t *wide = pattern == NULL ? NULL : path_to_wide(pattern);
    free(pattern);
    if (wide != NULL) {
        WIN32_FIND_DATAW found;
        HANDLE handle = FindFirstFileExW(wide, FindExInfoBasic, &found, FindExSearchNameMatch, NULL, 0);
        if (handle != INVALID_HANDLE_VALUE) {
            out->can_read = 1;
            FindClose(handle);
        } else {
            char why[300];
            win_reason(GetLastError(), why, sizeof(why));
            note(out, "error", "Cant read the folder: %s", why);
        }
        free(wide);
    }

    probe_write(out);
    probe_folder(out);

    if (out->have_space) {
        char shown[64];
        char wanted[64];
        gs_format_bytes(out->free_bytes, 1, shown, sizeof(shown));
        if (out->free_bytes < DIAG_CRITICAL_FREE) {
            note(out, "error", "Only %s free. An unpack wont fit.", shown);
        } else if (out->free_bytes < DIAG_RECOMMENDED_FREE) {
            gs_format_bytes(DIAG_RECOMMENDED_FREE, 1, wanted, sizeof(wanted));
            note(out, "warning", "%s free. A full unpack can want %s or more.", shown, wanted);
        }
    }
    if (out->protected_name[0] != 0) {
        note(out, "warning", "This sits inside %s, where Windows may block writes.", out->protected_name);
    }
    size_t length = code_points(out->path);
    if (length > DIAG_LONG_PATH) {
        note(out, "warning", "The path is %zu characters. Deep unpacks may hit the Windows path limit.",
             length);
    }
    return 1;
}

void diag_free(gs_diagnostics *d) {
    free(d->path);
    free(d->drive);
    free(d->issues);
    memset(d, 0, sizeof(*d));
}

static int has_severity(const gs_diagnostics *d, const char *severity) {
    for (size_t i = 0; i < d->issue_count; i++) {
        if (strcmp(d->issues[i].severity, severity) == 0) {
            return 1;
        }
    }
    return 0;
}

int diag_has_errors(const gs_diagnostics *d) {
    return has_severity(d, "error");
}

int diag_has_warnings(const gs_diagnostics *d) {
    return has_severity(d, "warning");
}

const char *diag_status(const gs_diagnostics *d) {
    if (diag_has_errors(d)) {
        return "Blocked";
    }
    return diag_has_warnings(d) ? "Warning" : "Good";
}

char *diag_report_text(const gs_diagnostics *d) {
    buf out;
    buf_init(&out);
    char free_text[64];
    char total_text[64];
    gs_format_bytes(d->free_bytes, d->have_space, free_text, sizeof(free_text));
    gs_format_bytes(d->total_bytes, d->have_space, total_text, sizeof(total_text));
    const char *drive = d->drive != NULL && d->drive[0] != 0 ? d->drive : "Unknown";
    buf_printf(&out, "Status: %s\nPath: %s\nDrive: %s\nFree space: %s of %s\n", diag_status(d), d->path,
               drive, free_text, total_text);
    buf_printf(&out, "Read: %s\nWrite file: %s\nCreate folder: %s\nDelete probe: %s",
               d->can_read ? "OK" : "Blocked", d->can_write ? "OK" : "Blocked",
               d->can_create ? "OK" : "Blocked", d->can_delete ? "OK" : "Blocked");
    if (d->protected_name != NULL && d->protected_name[0] != 0) {
        buf_printf(&out, "\nProtected location: %s", d->protected_name);
    }
    buf_puts(&out, "\n");
    if (d->issue_count > 0) {
        buf_puts(&out, "\nFindings:");
        for (size_t i = 0; i < d->issue_count; i++) {
            char upper[16];
            size_t k = 0;
            for (; d->issues[i].severity[k] && k + 1 < sizeof(upper); k++) {
                char ch = d->issues[i].severity[k];
                upper[k] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch;
            }
            upper[k] = 0;
            buf_printf(&out, "\n- %s: %s", upper, d->issues[i].message);
        }
    } else {
        buf_puts(&out, "\nNo problems found.");
    }
    if (out.data == NULL) {
        return _strdup("");
    }
    return out.data;
}
